// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT plugin: the PATTERN page. The track's 8 patterns (the one playing, one queued, the ones with notes),
// its length and the swing; a piano roll (synths) or a drum grid (BREAKS' slices, SAMPLER's pads) with a velocity
// lane; and the selected step's details: note, velocity, length, slide, accent, condition, chord or hit FX, retrig.
// Copyright (C) 2026 Jamie (TwistedGenetics).
#include "Pages.h"

namespace dn
{
static juce::String noteName(int v)
{
    static const char* const NN[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(NN[((v % 12) + 12) % 12]) + juce::String(v / 12 - 2);   // (C3 = 60, as the FM-1 names it)
}
static bool blackKey(int v) { const int k = v % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

// ------------------------------------------------------------------------------------------------- the grid ---
class PatternGrid : public juce::Component
{
public:
    PatternGrid(Ui& u, PatternPage& p) : ui(u), page(p) {}
    static constexpr int labelW = 70, velH = 72, rowH = 24;
    void load()
    {
        ui.core([&] {
            len = dnc_track_len(ui.track);
            pads = dnc_is_pads(ui.track) != 0;
            nrows = pads ? (int)dnc_pads(ui.track) : 128;
            for (int i = 0; i < 64; ++i)
                dnc_step_get(ui.track, i, &st[(size_t)i]);
            play = dnc_playing_step(ui.track);
            return 0;
        });
        if (nrows < 1)
            nrows = 1;
        if (!placed) {                               // (the first time: the notes in view)
            int lo = 127, hi = 0;
            for (int i = 0; i < len; ++i)
                if (st[(size_t)i].flags & DNC_ST_ON)
                    lo = juce::jmin(lo, (int)st[(size_t)i].note), hi = juce::jmax(hi, (int)st[(size_t)i].note);
            const int vis = visibleRows();
            top = pads ? 0 : juce::jlimit(0, 127 - vis + 1, (lo <= hi ? (lo + hi) / 2 : 48) + vis / 2);
            placed = true;
        }
        repaint();
    }
    void reset() { placed = false; }
    int visibleRows() const { return juce::jmax(1, (getHeight() - velH - 8) / rowH); }
    void paint(juce::Graphics& g) override
    {
        const auto gr = gridArea();
        const float cw = gr.getWidth() / (float)juce::jmax(1, len);
        const int vis = visibleRows();
        g.setColour(panel);
        g.fillRect(gr);
        for (int r = 0; r < vis; ++r) {              // the rows: notes (black keys shaded) or the slices / pads
            const int row = rowAt(r);
            if (row < 0)
                break;
            const float y = gr.getY() + r * rowH;
            if (!pads && blackKey(row)) {
                g.setColour(well);
                g.fillRect(gr.getX(), y, gr.getWidth(), (float)rowH);
            }
            g.setColour(faint);
            g.drawHorizontalLine((int)y, gr.getX(), gr.getRight());
            g.setColour(pads ? ink : blackKey(row) ? soft : ink);
            g.setFont(font(16.0f, pads || row % 12 == 0 ? 700 : 500));
            g.drawText(pads ? juce::String(row + 1) : noteName(row), 0, (int)y, labelW - 8, rowH, juce::Justification::centredRight);
        }
        for (int i = 0; i <= len; ++i) {             // the steps, beats, bars
            const float x = gr.getX() + i * cw;
            g.setColour(i % 16 == 0 ? ink.withAlpha(0.6f) : i % 4 == 0 ? grid : faint);
            g.drawVerticalLine((int)x, gr.getY(), gr.getBottom() + velH);
        }
        if (play >= 0 && play < len) {
            g.setColour(tint(hot, 0.35f));
            g.fillRect(gr.getX() + play * cw, gr.getY(), cw, gr.getHeight());
        }
        const juce::Colour col = deep(ui.accent);
        for (int i = 0; i < len; ++i) {              // the notes
            const auto& s = st[(size_t)i];
            if (!(s.flags & DNC_ST_ON))
                continue;
            const int r = rowIndex(s.note);
            if (r < 0 || r >= vis)
                continue;
            const float w = pads ? cw : juce::jmax(cw * 0.25f, cw * (float)s.len / 4.0f);
            auto rc = juce::Rectangle<float>(gr.getX() + i * cw, gr.getY() + r * rowH, juce::jmin(w, gr.getRight() - (gr.getX() + i * cw)), (float)rowH).reduced(1.5f, 2.0f);
            g.setColour(col.withAlpha(0.35f + 0.65f * (float)s.vel / 127.0f));
            g.fillRoundedRectangle(rc, 4.0f);
            g.setColour(i == page.selectedStep ? ink : col.darker(0.4f));
            g.drawRoundedRectangle(rc, 4.0f, i == page.selectedStep ? 2.5f : 1.0f);
            if (s.flags & DNC_ST_SLIDE) {
                g.setColour(ink);
                g.fillEllipse(rc.getX() + 2.0f, rc.getCentreY() - 3.0f, 6.0f, 6.0f);
            }
        }
        // the velocity lane
        auto vl = juce::Rectangle<float>(gr.getX(), gr.getBottom() + 8.0f, gr.getWidth(), (float)velH - 10.0f);
        g.setColour(paper);
        g.fillRect(vl);
        g.setColour(soft);
        g.setFont(font(15.0f, 700));
        g.drawText("VEL", 0, (int)vl.getY(), labelW - 8, 24, juce::Justification::centredRight);
        for (int i = 0; i < len; ++i) {
            const auto& s = st[(size_t)i];
            if (!(s.flags & DNC_ST_ON))
                continue;
            const float h = vl.getHeight() * (float)s.vel / 127.0f;
            g.setColour(i == page.selectedStep ? ink : col);
            g.fillRect(vl.getX() + i * cw + cw * 0.2f, vl.getBottom() - h, cw * 0.6f, h);
        }
        g.setColour(edge);
        g.drawRect(gr, 1.5f);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float dy = w.isReversed ? -w.deltaY : w.deltaY;
        const int maxTop = pads ? juce::jmax(0, nrows - visibleRows()) : 127;
        const int minTop = pads ? 0 : visibleRows() - 1;
        top = juce::jlimit(minTop, juce::jmax(minTop, maxTop), top + (pads ? (dy < 0 ? 1 : -1) : (dy > 0 ? 1 : -1)) * 2);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto gr = gridArea();
        mode = none;
        if (e.position.y > gr.getBottom()) {         // the velocity lane
            mode = vel;
            velAt(e.position);
            return;
        }
        const int i = colAt(e.position.x), r = (int)((e.position.y - gr.getY()) / rowH), row = rowAt(r);
        if (i < 0 || row < 0)
            return;
        auto& s = st[(size_t)i];
        const bool here = (s.flags & DNC_ST_ON) && s.note == row;
        if (e.mods.isPopupMenu() || (here && pads)) {   // (right-click, or a drum cell again: off)
            if (s.flags & DNC_ST_ON) {
                s.flags &= (uint8_t)~DNC_ST_ON;
                commit(i);
            }
            return;
        }
        if (here) {                                   // a note: picked; from its end: its length; up / down: pitch
            page.selectedStep = i;
            const float cw = gr.getWidth() / (float)len, endX = gr.getX() + (i + s.len / 4.0f) * cw;
            mode = std::abs(e.position.x - endX) < juce::jmax(8.0f, cw * 0.3f) || e.position.x > endX - 4.0f ? length : pitch;
            dragStep = i;
            page.stepChanged();
            repaint();
            return;
        }
        dnc_step_t d;                                 // a new note (or this step's note moved here)
        ui.core([&] { dnc_step_default(ui.track, &d); return 0; });
        if (s.flags & DNC_ST_ON)
            d = s;
        d.flags |= DNC_ST_ON;
        d.note = (uint8_t)row;
        s = d;
        page.selectedStep = i;
        dragStep = i;
        mode = pads ? none : length;
        commit(i);
        ui.core([&] { dnc_audition(ui.track, row, d.vel, 1); return 0; });
        heard = row;
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto gr = gridArea();
        if (mode == vel) {
            velAt(e.position);
            return;
        }
        if (dragStep < 0)
            return;
        auto& s = st[(size_t)dragStep];
        if (mode == length) {
            const float cw = gr.getWidth() / (float)len;
            const int q = juce::jlimit(1, 255, (int)std::lround((e.position.x - (gr.getX() + dragStep * cw)) / cw * 4.0f));
            if (q != s.len) {
                s.len = (uint8_t)q;
                commit(dragStep);
            }
        } else if (mode == pitch) {
            const int row = rowAt((int)((e.position.y - gr.getY()) / rowH));
            if (row >= 0 && row != s.note) {
                s.note = (uint8_t)row;
                commit(dragStep);
            }
        }
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (heard >= 0) {
            const int h = heard;
            ui.core([&] { dnc_audition(ui.track, h, 0, 0); return 0; });
        }
        heard = -1;
        mode = none;
        dragStep = -1;
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto gr = gridArea();
        const int i = colAt(e.position.x), row = rowAt((int)((e.position.y - gr.getY()) / rowH));
        if (i >= 0 && (st[(size_t)i].flags & DNC_ST_ON) && st[(size_t)i].note == row) {
            st[(size_t)i].flags &= (uint8_t)~DNC_ST_ON;   // (a note double-clicked: gone)
            commit(i);
        }
    }
    std::array<dnc_step_t, 64> st {};
    int len = 16;

private:
    juce::Rectangle<float> gridArea() const
    {
        return juce::Rectangle<float>((float)labelW, 0.0f, (float)getWidth() - labelW - 4.0f, (float)(visibleRows() * rowH));
    }
    int rowAt(int r) const
    {
        if (r < 0 || r >= visibleRows())
            return -1;
        const int v = pads ? top + r : top - r;
        return pads ? (v < nrows ? v : -1) : (v >= 0 && v <= 127 ? v : -1);
    }
    int rowIndex(int note) const { return pads ? note - top : top - note; }
    int colAt(float x) const
    {
        const auto gr = gridArea();
        const int i = (int)((x - gr.getX()) / (gr.getWidth() / (float)juce::jmax(1, len)));
        return x < gr.getX() || i >= len ? -1 : i;
    }
    void velAt(juce::Point<float> p)
    {
        const auto gr = gridArea();
        const int i = colAt(p.x);
        if (i < 0 || !(st[(size_t)i].flags & DNC_ST_ON))
            return;
        const float top0 = gr.getBottom() + 8.0f, h = (float)velH - 10.0f;
        const int v = juce::jlimit(1, 127, (int)std::lround((top0 + h - p.y) / h * 127.0f));
        if (v != st[(size_t)i].vel) {
            st[(size_t)i].vel = (uint8_t)v;
            page.selectedStep = i;
            commit(i);
        }
    }
    void commit(int i)
    {
        const dnc_step_t s = st[(size_t)i];
        ui.core([&] { dnc_step_set(ui.track, i, &s); return 0; });
        page.stepChanged();
        repaint();
    }
    Ui& ui;
    PatternPage& page;
    bool pads = false, placed = false;
    int nrows = 128, top = 72, play = -1, dragStep = -1, heard = -1;
    enum { none, length, pitch, vel } mode = none;
};

// -------------------------------------------------------------------------------- the step's details ---
class StepInspector : public juce::Component
{
public:
    StepInspector(Ui& u, PatternPage& p) : ui(u), page(p)
    {
        const juce::Colour c = ui.accent;
        int nc = 0, nr = 0, nch = 0, nhf = 0;
        ui.core([&] { nc = dnc_cond_count(), nr = dnc_rtim_count(), nch = dnc_chord_count(), nhf = dnc_hitfx_count(); return 0; });
        add(note = new FieldControl("NOTE", 0, 127, [](int v) { return noteName(v); }, false, c));
        add(velo = new FieldControl("VELOCITY", 1, 127, [](int v) { return juce::String(v); }, false, c));
        add(length = new FieldControl("LENGTH", 1, 255, [](int v) {
                return v % 4 == 0 ? juce::String(v / 4) + (v == 4 ? " STEP" : " STEPS") : juce::String(v / 4.0, 2) + " ST";
            }, false, c));
        add(slide = new FieldControl("SLIDE", 0, 1, [](int v) { return juce::String(v ? "ON" : "OFF"); }, true, c));
        add(accent = new FieldControl("ACCENT", 0, 1, [](int v) { return juce::String(v ? "ON" : "OFF"); }, true, c));
        add(cond = new FieldControl("CONDITION", 0, nc - 1, [this](int v) {
                char t[8] = { 0 };
                ui.core([&] { dnc_cond_text(v, t); return 0; });
                return v == 0 ? juce::String("ALWAYS") : juce::String(t);
            }, true, c));
        add(fxonly = new FieldControl("IF NOT", 0, 1, [](int v) { return juce::String(v ? "NO FX" : "SKIP"); }, true, c));
        add(chord = new FieldControl("CHORD", 0, nch - 1, [this](int v) {
                const char* n = "";
                ui.core([&] { n = dnc_chord_name(v); return 0; });
                return juce::String(n);
            }, true, c));
        add(hitfx = new FieldControl("HIT FX", 0, nhf - 1, [this](int v) {
                const char* n = "";
                ui.core([&] { n = dnc_hitfx_name(v); return 0; });
                return juce::String(n);
            }, true, c));
        add(rtrg = new FieldControl("RETRIG", 0, 1, [](int v) { return juce::String(v ? "ON" : "OFF"); }, true, c));
        add(rate = new FieldControl("RATE", 0, nr - 1, [this](int v) {
                int d = 16;
                ui.core([&] { d = dnc_rtim_den(v); return 0; });
                return "1/" + juce::String(d);
            }, true, c));
        add(rlen = new FieldControl("RETRIG LEN", 0, 255, [](int v) { return v ? juce::String(v / 4.0, 2) : juce::String("NOTE"); }, false, c));
        add(rvel = new FieldControl("CURVE", -127, 127, [](int v) { return (v > 0 ? "+" : "") + juce::String(v); }, false, c));
        note->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.note = (uint8_t)v; }); };
        velo->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.vel = (uint8_t)v; }); };
        length->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.len = (uint8_t)v; }); };
        slide->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.flags = (uint8_t)(v ? s.flags | DNC_ST_SLIDE : s.flags & ~DNC_ST_SLIDE); }); };
        accent->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.flags = (uint8_t)(v ? s.flags | DNC_ST_ACC : s.flags & ~DNC_ST_ACC); }); };
        cond->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.cond = (uint8_t)((s.cond & DNC_CO_FXONLY) | v); }); };
        fxonly->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.cond = (uint8_t)((s.cond & 0x7F) | (v ? DNC_CO_FXONLY : 0)); }); };
        auto top4 = [this](int v) { edit([v](dnc_step_t& s) { s.flags = (uint8_t)((s.flags & 0x0F) | (v << 4)); }); };
        chord->onChange = top4;
        hitfx->onChange = top4;
        rtrg->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.flags = (uint8_t)(v ? s.flags | DNC_ST_RTRG : s.flags & ~DNC_ST_RTRG); }); };
        rate->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.rtim = (uint8_t)v; }); };
        rlen->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.rlen = (uint8_t)v; }); };
        rvel->onChange = [this](int v) { edit([v](dnc_step_t& s) { s.rvel = (int8_t)v; }); };
    }
    ~StepInspector() override
    {
        for (auto* f : fields)
            delete f;
    }
    void show(const dnc_step_t* s, int idx)
    {
        step = idx;
        const bool on = s != nullptr && (s->flags & DNC_ST_ON);
        int chords = 0, hfx = 0;
        ui.core([&] { chords = dnc_track_chords(ui.track), hfx = dnc_track_hitfx(ui.track); return 0; });
        dnc_step_t z {};
        const dnc_step_t& v = s ? *s : z;
        note->set(v.note, on && !dnc_is_pads_cached);
        velo->set(v.vel, on);
        length->set(v.len, on && !dnc_is_pads_cached);
        slide->set((v.flags & DNC_ST_SLIDE) ? 1 : 0, on);
        accent->set((v.flags & DNC_ST_ACC) ? 1 : 0, on);
        cond->set(v.cond & 0x7F, on);
        fxonly->set((v.cond & DNC_CO_FXONLY) ? 1 : 0, on && (chords || hfx));
        chord->set(v.flags >> 4, on && chords);
        chord->setVisible(!hfx);
        hitfx->set(v.flags >> 4, on && hfx);
        hitfx->setVisible(hfx != 0);
        rtrg->set((v.flags & DNC_ST_RTRG) ? 1 : 0, on);
        rate->set(v.rtim, on && (v.flags & DNC_ST_RTRG));
        rlen->set(v.rlen, on && (v.flags & DNC_ST_RTRG));
        rvel->set(v.rvel, on && (v.flags & DNC_ST_RTRG));
        cur = v;
        repaint();
    }
    void resized() override
    {
        // two rows: the note (NOTE VELOCITY LENGTH SLIDE ACCENT CHORD / HIT FX), then when and how often (CONDITION
        // IF NOT RETRIG RATE RETRIG LEN CURVE)
        const std::vector<FieldControl*> row1 { note, velo, length, slide, accent, chord }, row2 { cond, fxonly, rtrg, rate, rlen, rvel };
        const float cw = (float)(getWidth() - 160) / 6.0f;
        for (int i = 0; i < 6; ++i) {
            row1[(size_t)i]->setBounds(150 + (int)(i * cw), 6, (int)cw - 6, 72);
            row2[(size_t)i]->setBounds(150 + (int)(i * cw), 82, (int)cw - 6, 72);
        }
        hitfx->setBounds(chord->getBounds());
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(panel);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(r, 10.0f, 1.5f);
        g.setColour(ink);
        g.setFont(font(22.0f, 700));
        g.drawText(step >= 0 ? "STEP " + juce::String(step + 1) : juce::String("STEP"), 14, 8, 130, 30, juce::Justification::centredLeft);
        g.setColour(soft);
        g.setFont(font(15.0f, 600));
        g.drawFittedText(step >= 0 ? "CLICK A NOTE\nTO SEE IT HERE" : "CLICK A NOTE\nIN THE GRID", 14, 40, 130, 36, juce::Justification::topLeft, 2);
    }
    bool dnc_is_pads_cached = false;

private:
    void add(FieldControl* f)
    {
        fields.push_back(f);
        addAndMakeVisible(f);
    }
    template <class F> void edit(F f)
    {
        if (step < 0)
            return;
        f(cur);
        const dnc_step_t s = cur;
        const int i = step;
        ui.core([&] { dnc_step_set(ui.track, i, &s); return 0; });
        page.stepChanged();
    }
    Ui& ui;
    PatternPage& page;
    std::vector<FieldControl*> fields;
    FieldControl *note, *velo, *length, *slide, *accent, *cond, *fxonly, *chord, *hitfx, *rtrg, *rate, *rlen, *rvel;
    int step = -1;
    dnc_step_t cur {};
};

// ------------------------------------------------------------------------------------------------ the page ---
PatternPage::PatternPage(Ui& u) : Page(u)
{
    for (int i = 0; i < 8; ++i) {
        patBtn[(size_t)i] = std::make_unique<juce::TextButton>(juce::String(i + 1));
        patBtn[(size_t)i]->onClick = [this, i] {
            ui.core([&] { dnc_pat_select(ui.track, i); return 0; });
            selectedStep = -1;
            if (grid) grid->reset();
            poll();
        };
        addAndMakeVisible(*patBtn[(size_t)i]);
    }
    lenField = std::make_unique<FieldControl>("LENGTH", 1, 64, [](int v) { return juce::String(v) + " STEPS"; }, false, ui.accent);
    lenField->onChange = [this](int v) {
        ui.core([&] { dnc_track_set_len(ui.track, v); return 0; });
        poll();
    };
    swingField = std::make_unique<FieldControl>("SWING", 50, 75, [](int v) { return juce::String(v) + "%"; }, false, ui.accent);
    swingField->onChange = [this](int v) {
        ui.core([&] { dnc_set_swing(v); return 0; });
        poll();
    };
    addAndMakeVisible(*lenField);
    addAndMakeVisible(*swingField);
}
PatternPage::~PatternPage() = default;

void PatternPage::rebuild()
{
    grid = std::make_unique<PatternGrid>(ui, *this);
    inspector = std::make_unique<StepInspector>(ui, *this);
    addAndMakeVisible(*grid);
    addAndMakeVisible(*inspector);
    selectedStep = -1;
    resized();
    poll();
}

void PatternPage::stepChanged()
{
    if (!grid || !inspector)
        return;
    grid->load();
    ui.core([&] { inspector->dnc_is_pads_cached = dnc_is_pads(ui.track) != 0; return 0; });
    inspector->show(selectedStep >= 0 && selectedStep < grid->len ? &grid->st[(size_t)selectedStep] : nullptr, selectedStep);
}

void PatternPage::poll()
{
    int cur = 0, q = -1, l = 16, sw = 50;
    uint32_t used = 0;
    ui.core([&] {
        cur = dnc_pat_current(ui.track), q = dnc_pat_queued(ui.track), used = dnc_pat_used(ui.track);
        l = dnc_track_len(ui.track);
        sw = dnc_swing();
        return 0;
    });
    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    for (int i = 0; i < 8; ++i) {
        auto& b = *patBtn[(size_t)i];
        b.setToggleState(i == cur || (i == q && blink), juce::dontSendNotification);
        b.setButtonText(juce::String(i + 1) + ((used >> i) & 1u ? "*" : ""));
    }
    lenField->set(l, true);
    swingField->set(sw, true);
    stepChanged();
}

void PatternPage::resized()
{
    for (int i = 0; i < 8; ++i)
        patBtn[(size_t)i]->setBounds(i * 56, 26, 52, 40);
    lenField->setBounds(8 * 56 + 16, 0, 130, 72);
    swingField->setBounds(8 * 56 + 156, 0, 110, 72);
    if (inspector)
        inspector->setBounds(0, getHeight() - 162, getWidth(), 162);
    if (grid)
        grid->setBounds(0, 82, getWidth(), getHeight() - 82 - 170);
}

void PatternPage::paint(juce::Graphics& g)
{
    g.setColour(ink);
    g.setFont(font(16.0f, 600));
    g.drawText("PATTERN (* HAS NOTES; PICKED WHILE PLAYING: NEXT)", 0, 0, 8 * 56, 20, juce::Justification::centredLeft);
    g.setColour(soft);
    g.setFont(font(15.0f, 500));
    g.drawFittedText("CLICK: A NOTE   DRAG ITS END: LENGTH   DRAG IT: PITCH   DOUBLE- / RIGHT-CLICK: GONE   SCROLL: UP / DOWN",
                     8 * 56 + 280, 8, juce::jmax(80, getWidth() - (8 * 56 + 280)), 60, juce::Justification::centredLeft, 3);
}
}
