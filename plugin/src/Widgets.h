// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT plugin: the controls. A value of the core (an id, and the pad / zone / operator / LFO / slot / track it
// belongs to) shown as a knob, or as a list when its values are words; a section of them in a panel, as the FM-1's
// pages group them. Every read and write goes through the processor's withCore. Copyright (C) 2026 Jamie
// (TwistedGenetics).
#pragma once
#include "PluginProcessor.h"
#include "Theme.h"
#include "dn_core.h"
#include <functional>

namespace dn
{
// what the pages share: the processor, the track shown, each selector's choice (pad, zone, operator ...)
struct Ui
{
    DreadnoughtProcessor& proc;
    int track = 0;
    std::array<int, 8> selIdx {};
    juce::Colour accent { hot };
    std::function<void()> onStructure;               // (an instrument changed, a mode: the page built again)
    template <class F> auto core(F&& f) { return proc.withCore(std::forward<F>(f)); }
};

inline bool isWordy(const char* t)
{
    if (!t || !t[0])
        return false;
    const char c = t[0] == '+' || t[0] == '-' ? t[1] : t[0];
    return !(c >= '0' && c <= '9');
}

// ------------------------------------------------------------------------------------------- a value: a knob ---
class ValueControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueControl(Ui& u, uint32_t id, int sel) : ui(u), vid(id), vsel(sel) { refresh(); }
    std::function<void()> onChange;
    int selIndex() const { return fixedIdx >= 0 ? fixedIdx : ui.selIdx[(size_t)vsel]; }
    void setFixedIndex(int i) { fixedIdx = i; }      // (this pad / operator / LFO / track always, not the selector's)
    void setCompact(bool c) { compact = c; }         // (in a table: no name, a smaller knob)
    void refresh()
    {
        char t[16];
        juce::String nm;
        int32_t v = 0, lo = 0, hi = 0;
        ui.core([&] {
            const int i = selIndex();
            v = dnc_get(vid, vsel, i), lo = dnc_min(vid, vsel, i), hi = dnc_max(vid, vsel, i);
            nm = dnc_name(vid, vsel, i);
            dnc_text(vid, vsel, i, v, t);
            if (!probed) {                           // (a list when its values are words and there are few of them)
                char a[16];
                dnc_text(vid, vsel, i, lo, a);
                list = (hi - lo <= 40 && isWordy(a) && isWordy(t)) || (hi - lo == 1);
                probed = true;
            }
            return 0;
        });
        if (v != val || lo != vlo || hi != vhi || nm != name || juce::String(t) != text) {
            val = v, vlo = lo, vhi = hi, name = nm, text = t;
            repaint();
        }
    }
    void setValue(int32_t v)
    {
        v = juce::jlimit(vlo, vhi, v);
        if (v == val)
            return;
        ui.core([&] {
            dnc_set(vid, vsel, selIndex(), v);
            return 0;
        });
        refresh();
        if (onChange)
            onChange();
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const juce::Colour col = deep(ui.accent);
        g.setColour(ink);
        g.setFont(font(16.0f, 600));
        if (!compact)
            g.drawFittedText(name, getLocalBounds().removeFromTop(20), juce::Justification::centred, 1, 0.8f);
        if (list) {                                  // a list: its word in a box, ▾
            auto b = compact ? r.withSizeKeepingCentre(r.getWidth() - 4.0f, 36.0f) : r.withTrimmedTop(30.0f).withHeight(40.0f).reduced(3.0f, 0.0f);
            g.setColour(panel);
            g.fillRoundedRectangle(b, 6.0f);
            g.setColour(dragging ? ink : edge);
            g.drawRoundedRectangle(b, 6.0f, dragging ? 2.0f : 1.5f);
            g.setColour(col);
            g.fillRoundedRectangle(b.removeFromLeft(6.0f), 3.0f);
            g.setColour(ink);
            g.setFont(font(19.0f, 700));
            g.drawFittedText(text, b.reduced(6.0f, 0.0f).toNearestInt().withTrimmedRight(12), juce::Justification::centred, 1, 0.7f);
            juce::Path tri;
            const float x = b.getRight() - 14.0f, y = b.getCentreY();
            tri.addTriangle(x, y - 3.0f, x + 9.0f, y - 3.0f, x + 4.5f, y + 3.5f);
            g.setColour(soft);
            g.fillPath(tri);
            return;
        }
        const float d = compact ? juce::jmin(r.getWidth() - 10.0f, r.getHeight() - 24.0f, 44.0f) : juce::jmin(r.getWidth() - 14.0f, 56.0f);
        const juce::Rectangle<float> k(r.getCentreX() - d / 2.0f, compact ? 2.0f : 23.0f, d, d);
        const float a0 = juce::MathConstants<float>::pi * -0.75f, a1 = juce::MathConstants<float>::pi * 0.75f;
        const float f = vhi > vlo ? (float)(val - vlo) / (float)(vhi - vlo) : 0.0f;
        const float zf = vlo < 0 && vhi > 0 ? (float)(-vlo) / (float)(vhi - vlo) : 0.0f;
        juce::Path track, arc;
        track.addCentredArc(k.getCentreX(), k.getCentreY(), d / 2.0f - 3.0f, d / 2.0f - 3.0f, 0.0f, a0, a1, true);
        g.setColour(well);
        g.strokePath(track, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float fa = a0 + (a1 - a0) * juce::jmin(f, zf), fb = a0 + (a1 - a0) * juce::jmax(f, zf);
        if (fb > fa + 0.001f) {
            arc.addCentredArc(k.getCentreX(), k.getCentreY(), d / 2.0f - 3.0f, d / 2.0f - 3.0f, 0.0f, fa, fb, true);
            g.setColour(col);
            g.strokePath(arc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(dragging ? paper : panel);
        g.fillEllipse(k.reduced(9.0f));
        g.setColour(edge);
        g.drawEllipse(k.reduced(9.0f), 1.5f);
        const float ang = a0 + (a1 - a0) * f;
        g.setColour(ink);
        g.drawLine(k.getCentreX() + std::sin(ang) * d * 0.12f, k.getCentreY() - std::cos(ang) * d * 0.12f,
                   k.getCentreX() + std::sin(ang) * (d * 0.5f - 11.0f), k.getCentreY() - std::cos(ang) * (d * 0.5f - 11.0f),
                   3.0f);
        g.setFont(font(compact ? 17.0f : 19.0f, 700));
        g.drawFittedText(text, getLocalBounds().removeFromBottom(compact ? 22 : 26), juce::Justification::centred, 1, 0.7f);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (list) {
            showList();
            return;
        }
        dragging = true;
        lastY = e.y;
        acc = 0.0;
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (list)
            return;
        const int range = vhi - vlo;
        // the whole range in 200 px (shift: 1000 px); a short one a step every 10 px
        const double per = range <= 20 ? 10.0 : 200.0 / juce::jmax(1, range);
        acc += (double)(lastY - e.y) / (e.mods.isShiftDown() ? per * 5.0 : per);
        lastY = e.y;
        const int d = (int)acc;
        if (d != 0) {
            acc -= d;
            setValue(val + d);
        }
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (list)
            return;
        auto* ed = new juce::TextEditor();             // a number typed in
        ed->setFont(font(20.0f, 700));
        ed->setJustification(juce::Justification::centred);
        ed->setText(juce::String(val), false);
        ed->selectAll();
        ed->setSize(90, 34);
        juce::Component::SafePointer<ValueControl> self(this);
        ed->onReturnKey = [self, ed] {
            if (self != nullptr)
                self->setValue(ed->getText().getIntValue());
            if (auto* box = ed->findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
        };
        juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(ed), getScreenBounds(), nullptr);
        ed->grabKeyboardFocus();
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float dy = w.isReversed ? -w.deltaY : w.deltaY;
        if (std::abs(dy) < 0.0001f)
            return;
        const int range = vhi - vlo, unit = range > 100 && !e.mods.isShiftDown() ? range / 50 : 1;
        setValue(val + (dy > 0 ? unit : -unit));
    }
    void showList()
    {
        juce::PopupMenu m;
        std::vector<int32_t> ch(256);
        std::vector<juce::String> names;
        uint32_t n = 0;
        ui.core([&] {
            n = dnc_choices(vid, vsel, selIndex(), ch.data(), 256);
            for (uint32_t i = 0; i < n; ++i) {
                char t[16];
                dnc_text(vid, vsel, selIndex(), ch[i], t);
                names.push_back(t);
            }
            return 0;
        });
        for (uint32_t i = 0; i < n; ++i)
            m.addItem((int)i + 1, names[i], true, ch[i] == val);
        juce::Component::SafePointer<ValueControl> self(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                        [self, ch](int r) {
                            if (self != nullptr && r > 0)
                                self->setValue(ch[(size_t)r - 1]);
                        });
    }
    uint32_t id() const { return vid; }

private:
    Ui& ui;
    uint32_t vid;
    int vsel, fixedIdx = -1;
    bool compact = false;
    int32_t val = INT32_MIN, vlo = 0, vhi = 0;
    juce::String name, text;
    bool list = false, probed = false, dragging = false;
    int lastY = 0;
    double acc = 0.0;
};

// a number of the step (not a core value): a knob, or a list when its values are words
class FieldControl : public juce::Component
{
public:
    FieldControl(juce::String n, int lo, int hi, std::function<juce::String(int)> txt, bool asList, juce::Colour c)
        : name(std::move(n)), vlo(lo), vhi(hi), textOf(std::move(txt)), list(asList), col(c) {}
    std::function<void(int)> onChange;
    void set(int v, bool on)
    {
        if (v != val || on != enabled) {
            val = v, enabled = on;
            repaint();
        }
    }
    void setRange(int lo, int hi) { vlo = lo, vhi = hi; }
    void paint(juce::Graphics& g) override
    {
        g.setColour(enabled ? ink : soft.withAlpha(0.5f));
        g.setFont(font(16.0f, 600));
        g.drawFittedText(name, getLocalBounds().removeFromTop(20), juce::Justification::centred, 1, 0.8f);
        auto b = getLocalBounds().toFloat().withTrimmedTop(26.0f).withHeight(40.0f).reduced(3.0f, 0.0f);
        g.setColour(panel);
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(b, 6.0f, 1.5f);
        if (!list && vhi > vlo && enabled) {         // (a bar: where the value is in its range)
            const float f = (float)(val - vlo) / (float)(vhi - vlo);
            g.setColour(tint(col, 0.5f));
            g.fillRoundedRectangle(b.withWidth(juce::jmax(6.0f, b.getWidth() * f)).reduced(1.5f), 5.0f);
        }
        g.setColour(enabled ? ink : soft.withAlpha(0.5f));
        g.setFont(font(20.0f, 700));
        g.drawFittedText(enabled ? textOf(val) : "-", b.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!enabled)
            return;
        if (list) {
            juce::PopupMenu m;
            for (int v = vlo; v <= vhi; ++v)
                m.addItem(v - vlo + 1, textOf(v), true, v == val);
            juce::Component::SafePointer<FieldControl> self(this);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                            [self](int r) { if (self != nullptr && r > 0) self->change(self->vlo + r - 1); });
            return;
        }
        lastX = e.x, lastY = e.y, acc = 0.0;
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (list || !enabled)
            return;
        const double per = vhi - vlo <= 24 ? 10.0 : 200.0 / juce::jmax(1, vhi - vlo);
        acc += (double)((e.x - lastX) + (lastY - e.y)) / (e.mods.isShiftDown() ? per * 5.0 : per);
        lastX = e.x, lastY = e.y;
        const int d = (int)acc;
        if (d) {
            acc -= d;
            change(val + d);
        }
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float dy = w.isReversed ? -w.deltaY : w.deltaY;
        if (enabled && std::abs(dy) > 0.0001f)
            change(val + (dy > 0 ? 1 : -1));
    }
    void change(int v)
    {
        v = juce::jlimit(vlo, vhi, v);
        if (v == val)
            return;
        val = v;
        repaint();
        if (onChange)
            onChange(v);
    }
private:
    juce::String name;
    int vlo, vhi, val = 0;
    std::function<juce::String(int)> textOf;
    bool list, enabled = true;
    juce::Colour col;
    int lastX = 0, lastY = 0;
    double acc = 0.0;
};

// ------------------------------------------------------------------------------------ a section, in a panel ---
inline int selCount(int sel) { return sel == DNC_SEL_PAD ? 16 : sel == DNC_SEL_ZONE ? 8 : sel == DNC_SEL_OP ? 6 : sel == DNC_SEL_LFO ? 4 : sel == DNC_SEL_MSLOT ? 8 : 0; }
inline const char* selName(int sel) { return sel == DNC_SEL_PAD ? "PAD" : sel == DNC_SEL_ZONE ? "ZONE" : sel == DNC_SEL_OP ? "OP" : sel == DNC_SEL_LFO ? "LFO" : "SLOT"; }

class SectionPanel : public juce::Component
{
public:
    static constexpr int cellW = 92, cellH = 112, headH = 40, selH = 38;
    SectionPanel(Ui& u, const dnc_section_t& s, std::function<void()> selChanged, bool selectorShown = true)
        : ui(u), sel(s.sel), title(s.name)
    {
        for (int i = 0; i < s.n; ++i) {
            auto c = std::make_unique<ValueControl>(ui, s.id[i], s.sel);
            addAndMakeVisible(*c);
            controls.push_back(std::move(c));
        }
        for (int i = 0; selectorShown && i < selCount(sel); ++i) {
            auto b = std::make_unique<juce::TextButton>(juce::String(i + 1));
            b->onClick = [this, i, selChanged] {
                ui.selIdx[(size_t)sel] = i;
                if (selChanged)
                    selChanged();
            };
            addAndMakeVisible(*b);
            selButtons.push_back(std::move(b));
        }
    }
    int perRow() const { return (int)controls.size() > 4 ? ((int)controls.size() + 1) / 2 : (int)controls.size(); }
    int rows() const { return (int)controls.size() > 4 ? 2 : 1; }
    int selRows() const { return selButtons.empty() ? 0 : ((int)selButtons.size() * 40 + 16 > preferredWidthBase() ? 2 : 1); }
    int preferredWidthBase() const { return juce::jmax(16 + perRow() * cellW, 200); }
    int preferredWidth() const { return juce::jmax(preferredWidthBase(), selButtons.empty() ? 0 : juce::jmin(16 + (int)selButtons.size() * 40, 16 + 8 * 40)); }
    int preferredHeight() const { return headH + selRows() * selH + rows() * cellH + 12; }
    void refresh()
    {
        for (auto& c : controls)
            c->refresh();
        for (int i = 0; i < (int)selButtons.size(); ++i)
            selButtons[(size_t)i]->setToggleState(ui.selIdx[(size_t)sel] == i, juce::dontSendNotification);
        repaint();
    }
    void resized() override
    {
        const int n = (int)selButtons.size(), perSel = selRows() == 2 ? (n + 1) / 2 : n;
        for (int i = 0; i < n; ++i)                   // (the selector: under the head, one row or two)
            selButtons[(size_t)i]->setBounds(8 + (i % juce::jmax(1, perSel)) * 40, headH + (i / juce::jmax(1, perSel)) * selH, 36, 34);
        const int pr = perRow(), top = headH + selRows() * selH;
        const float cw = (float)(getWidth() - 16) / (float)juce::jmax(1, pr);   // (the knobs spread over the width)
        for (int i = 0; i < (int)controls.size(); ++i)
            controls[(size_t)i]->setBounds(8 + (int)((i % pr) * cw + (cw - cellW) / 2), top + (i / pr) * cellH + 4, cellW, cellH);
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(panel);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(r, 10.0f, 1.5f);
        auto h = r.withHeight((float)headH - 4.0f).reduced(4.0f, 4.0f);
        g.setColour(tint(ui.accent, 0.35f));
        g.fillRoundedRectangle(h, 7.0f);
        g.setColour(ink);
        g.setFont(font(20.0f, 700));
        juce::String t = title;
        if (selCount(sel) && !t.contains(":"))
            t << ": " << selName(sel) << " " << (ui.selIdx[(size_t)sel] + 1);
        else if (selCount(sel))
            t << " (" << selName(sel) << " " << (ui.selIdx[(size_t)sel] + 1) << ")";
        g.drawFittedText(t, h.reduced(10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }
    int selection() const { return sel; }

private:
    Ui& ui;
    int sel;
    juce::String title;
    std::vector<std::unique_ptr<ValueControl>> controls;
    std::vector<std::unique_ptr<juce::TextButton>> selButtons;
};

// the values of every operator (LFO, matrix slot) at once: a table, a row each, a column a value
class MatrixPanel : public juce::Component
{
public:
    static constexpr int rowH = 72, headH = 40, colHeadH = 24, labelW = 70;
    MatrixPanel(Ui& u, juce::String t, int s, const std::vector<uint16_t>& ids) : ui(u), title(std::move(t)), sel(s)
    {
        for (int r = 0; r < selCount(sel); ++r)
            for (auto id : ids) {
                auto c = std::make_unique<ValueControl>(ui, id, sel);
                c->setFixedIndex(r);
                c->setCompact(true);
                c->refresh();
                addAndMakeVisible(*c);
                ctl.push_back(std::move(c));
            }
        ncol = (int)ids.size();
        ui.core([&] {
            for (auto id : ids)
                names.add(dnc_name(id, sel, 0));
            return 0;
        });
    }
    int preferredHeight() const { return headH + colHeadH + selCount(sel) * rowH + 10; }
    void refresh()
    {
        for (auto& c : ctl)
            c->refresh();
    }
    void resized() override
    {
        const float cw = (float)(getWidth() - labelW - 12) / (float)juce::jmax(1, ncol);
        for (size_t i = 0; i < ctl.size(); ++i) {
            const int r = (int)i / ncol, c = (int)i % ncol;
            ctl[i]->setBounds(labelW + (int)(c * cw), headH + colHeadH + r * rowH + 4, (int)cw, rowH - 6);
        }
    }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(panel);
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(r, 10.0f, 1.5f);
        auto h = r.withHeight((float)headH - 4.0f).reduced(4.0f, 4.0f);
        g.setColour(tint(ui.accent, 0.35f));
        g.fillRoundedRectangle(h, 7.0f);
        g.setColour(ink);
        g.setFont(font(20.0f, 700));
        g.drawText(title, h.reduced(10.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        const float cw = (float)(getWidth() - labelW - 12) / (float)juce::jmax(1, ncol);
        g.setFont(font(16.0f, 600));
        for (int c = 0; c < ncol; ++c)
            g.drawFittedText(names[c], labelW + (int)(c * cw), headH, (int)cw, colHeadH, juce::Justification::centred, 1, 0.7f);
        for (int row = 0; row < selCount(sel); ++row) {
            const int y = headH + colHeadH + row * rowH;
            if (row % 2) {
                g.setColour(paper);
                g.fillRect(6, y, getWidth() - 12, rowH);
            }
            g.setColour(ink);
            g.setFont(font(20.0f, 700));
            g.drawText(juce::String(selName(sel)) + " " + juce::String(row + 1), 10, y, labelW - 10, rowH, juce::Justification::centredLeft);
        }
    }
private:
    Ui& ui;
    juce::String title;
    int sel, ncol = 0;
    juce::StringArray names;
    std::vector<std::unique_ptr<ValueControl>> ctl;
};

// a column of panels laid out in rows, as wide as there is room (a flow), in a scrolling view
class FlowView : public juce::Viewport
{
public:
    FlowView()
    {
        setViewedComponent(&content, false);
        setScrollBarsShown(true, false);
        setScrollBarThickness(12);
    }
    void clear()
    {
        panels.clear();
        tails.clear();
        firsts.clear();
        heads.clear();
        content.removeAllChildren();
    }
    void addHead(juce::Component* c, int h)          // (a full-width part first: a waveform, the pads)
    {
        heads.push_back({ c, h });
        content.addAndMakeVisible(c);
    }
    void addTail(std::unique_ptr<MatrixPanel> m, bool first = false)   // (a table across the width: after the
    {                                                                  //  panels, or first)
        content.addAndMakeVisible(*m);
        (first ? firsts : tails).push_back(std::move(m));
    }
    void add(std::unique_ptr<SectionPanel> p)
    {
        content.addAndMakeVisible(*p);
        panels.push_back(std::move(p));
    }
    void refresh()
    {
        for (auto& p : panels)
            p->refresh();
        for (auto& m : tails)
            m->refresh();
        for (auto& m : firsts)
            m->refresh();
    }
    void layout()
    {
        const int w = getWidth() - getScrollBarThickness() - 6;   // (room for the scroll bar, there or not)
        int y = 0;
        for (auto& h : heads) {
            h.first->setBounds(0, y, w, h.second);
            y += h.second + 12;
        }
        for (auto& m : firsts) {
            m->setBounds(0, y, w, m->preferredHeight());
            y += m->preferredHeight() + 12;
        }
        size_t i = 0;
        while (i < panels.size()) {                  // (a row: as many as fit, then stretched to the width)
            size_t j = i;
            int used = 0, rowH = 0;
            while (j < panels.size()) {
                const int pw = juce::jmin(panels[j]->preferredWidth(), w);
                if (j > i && used + 12 + pw > w)
                    break;
                used += (j > i ? 12 : 0) + pw;
                rowH = juce::jmax(rowH, panels[j]->preferredHeight());
                ++j;
            }
            const int spare = w - used, n = (int)(j - i);
            int x = 0;
            for (size_t k = i; k < j; ++k) {
                const int pw = juce::jmin(panels[k]->preferredWidth(), w) + spare / n + ((int)(k - i) < spare % n ? 1 : 0);
                panels[k]->setBounds(x, y, pw, rowH);
                x += pw + 12;
            }
            y += rowH + 12;
            i = j;
        }
        for (auto& m : tails) {
            m->setBounds(0, y, w, m->preferredHeight());
            y += m->preferredHeight() + 12;
        }
        content.setSize(w, y + 4);
    }
    void resized() override
    {
        juce::Viewport::resized();
        layout();
    }

private:
    juce::Component content;
    std::vector<std::unique_ptr<SectionPanel>> panels;
    std::vector<std::unique_ptr<MatrixPanel>> tails, firsts;
    std::vector<std::pair<juce::Component*, int>> heads;
};
}
