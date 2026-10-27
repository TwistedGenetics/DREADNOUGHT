// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the editor. Copyright (C) 2026 Jamie (TwistedGenetics).
#include "PluginEditor.h"
#include "dn_core.h"

using namespace dn;

namespace
{
juce::Colour rgb(uint32_t c) { return juce::Colour(0xFF000000u | c); }
}

// ------------------------------------------------------------------------------------------------ the top bar ---
class TopBar : public juce::Component
{
public:
    explicit TopBar(Ui& u) : ui(u)
    {
        prev.onClick = [this] { step(-1); };
        next.onClick = [this] { step(1); };
        presetBtn.onClick = [this] { presetMenu(); };
        store.onClick = [this] { storeMenu(); };
        play.onClick = [this] {
            ui.core([&] { return dnc_transport_toggle(); });
            poll();
        };
        follow.setButtonText("FOLLOW THE DAW");
        follow.onClick = [this] { ui.proc.syncToHost = follow.getToggleState(); };
        web.onClick = [] { juce::URL("https://twistedgenetics.github.io/DREADNOUGHT/editor/").launchInDefaultBrowser(); };
        follow.setClickingTogglesState(true);
        for (auto* b : { &prev, &next, &presetBtn, &store, &play, &follow, &web })
            addAndMakeVisible(b);
        bpm = std::make_unique<FieldControl>("BPM", 60, 200, [](int v) { return juce::String(v); }, false, hot);
        bpm->onChange = [this](int v) { ui.core([&] { dnc_set(dnc_id_bpm(), 0, 0, v); return 0; }); };
        addAndMakeVisible(*bpm);
    }
    void poll()
    {
        int cur = -1, b = 120, playing = 0, syn = 0;
        uint32_t n = 0;
        juce::String nm, msg;
        ui.core([&] {
            dnc_track_select(ui.track);
            n = dnc_preset_count();
            cur = dnc_preset_current();
            nm = cur >= 0 && (uint32_t)cur < n ? juce::String(dnc_preset_name((uint32_t)cur)) : juce::String();
            b = dnc_get(dnc_id_bpm(), 0, 0);
            playing = dnc_playing();
            const int e = dnc_track_engine(ui.track);
            syn = e == 1 || e == 5;                  // (REESE, SUB: user sounds)
            msg = dnc_message();
            return 0;
        });
        presetBtn.setButtonText(n ? nm : juce::String("NO PRESETS"));
        presetBtn.setEnabled(n > 0);
        prev.setEnabled(n > 0);
        next.setEnabled(n > 0);
        store.setVisible(syn != 0);
        bpm->set(b, true);
        play.setButtonText(playing ? "STOP" : "PLAY");
        play.setToggleState(playing != 0, juce::dontSendNotification);
        follow.setToggleState(ui.proc.syncToHost.load(), juce::dontSendNotification);
        const auto ls = ui.proc.linkState();
        const juce::String ws = ls == DreadnoughtProcessor::linkTalking ? "WEB EDITOR: CONNECTED"
                              : ls == DreadnoughtProcessor::linkReady ? "WEB EDITOR" : "WEB EDITOR (NO PORT)";
        web.setButtonText(ws);
        if (msg != message) {
            message = msg;
            repaint();
        }
    }
    void resized() override
    {
        prev.setBounds(236, 14, 44, 44);
        presetBtn.setBounds(284, 14, 300, 44);
        next.setBounds(588, 14, 44, 44);
        store.setBounds(640, 14, 150, 44);
        bpm->setBounds(806, 0, 96, 70);
        play.setBounds(914, 14, 96, 44);
        follow.setBounds(1016, 14, 116, 44);
        web.setBounds(1138, 14, 128, 44);
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(panel);
        g.fillRect(getLocalBounds());
        g.setColour(edge);
        g.drawHorizontalLine(getHeight() - 1, 0.0f, (float)getWidth());
        g.setColour(ink);
        g.setFont(font(34.0f, 700));
        g.drawText("DREADNOUGHT", 16, 8, 210, 40, juce::Justification::centredLeft);
        g.setColour(soft);
        g.setFont(font(15.0f, 600));
        g.drawText("v" DN_VERSION_STR + juce::String(message.isNotEmpty() ? "   " + message : juce::String()), 16, 46, 760, 22,
                   juce::Justification::centredLeft);
    }
private:
    void step(int d)
    {
        ui.core([&] {
            const uint32_t n = dnc_preset_count();
            if (n) {
                int c = dnc_preset_current();
                for (uint32_t k = 0; k < n; ++k) {   // (the next one with a sound in it)
                    c = (int)(((uint32_t)(c + d) + n) % n);
                    if (dnc_preset_used((uint32_t)c))
                        break;
                }
                dnc_preset_load((uint32_t)c);
            }
            return 0;
        });
        poll();
    }
    void presetMenu()
    {
        juce::PopupMenu m;
        ui.core([&] {
            const uint32_t n = dnc_preset_count();
            const int cur = dnc_preset_current();
            for (uint32_t i = 0; i < n; ++i)
                m.addItem((int)i + 1, dnc_preset_name(i), dnc_preset_used(i) != 0, (int)i == cur);
            return 0;
        });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn), [this](int r) {
            if (r > 0) {
                ui.core([&] { dnc_preset_load((uint32_t)r - 1); return 0; });
                poll();
            }
        });
    }
    void storeMenu()
    {
        juce::PopupMenu m;
        for (int u = 0; u < 8; ++u)
            m.addItem(u + 1, "INTO USER " + juce::String(u + 1));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&store), [this](int r) {
            if (r <= 0)
                return;
            auto* w = new juce::AlertWindow("KEEP THIS SOUND", "Its name (8 letters):", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor("name", "", "NAME");
            w->addButton("KEEP", 1);
            w->addButton("CANCEL", 0);
            w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, r](int ok) {
                if (ok) {
                    const auto nm = w->getTextEditorContents("name").substring(0, 8);
                    ui.core([&] { return dnc_user_store((uint32_t)r - 1, nm.toRawUTF8()); });
                    poll();
                }
            }), true);
        });
    }
    Ui& ui;
    juce::TextButton prev { "<" }, next { ">" }, presetBtn, store { "KEEP AS USER..." }, play { "PLAY" }, follow, web;
    std::unique_ptr<FieldControl> bpm;
    juce::String message;
};

// ---------------------------------------------------------------------------------------------- the tracks ---
class TrackList : public juce::Component
{
public:
    TrackList(Ui& u, std::function<void(int)> picked) : ui(u), onPick(std::move(picked)) {}
    static constexpr int rowH = 78;
    void poll()
    {
        std::array<juce::String, 8> nm;
        std::array<uint32_t, 8> col {};
        std::array<int, 8> mute {}, pat {}, eng {};
        ui.core([&] {
            for (int t = 0; t < 8; ++t) {
                nm[(size_t)t] = dnc_track_name(t), col[(size_t)t] = dnc_track_colour(t);
                mute[(size_t)t] = dnc_track_mute(t), pat[(size_t)t] = dnc_pat_current(t), eng[(size_t)t] = dnc_track_engine(t);
            }
            return 0;
        });
        if (nm != names || col != cols || mute != mutes || pat != pats || ui.track != shownSel) {
            names = nm, cols = col, mutes = mute, pats = pat, engs = eng, shownSel = ui.track;
            repaint();
        }
    }
    void paint(juce::Graphics& g) override
    {
        for (int t = 0; t < 8; ++t) {
            auto r = juce::Rectangle<float>(0.0f, (float)(t * rowH), (float)getWidth(), (float)rowH).reduced(0.0f, 3.0f);
            const juce::Colour c = rgb(cols[(size_t)t]);
            const bool sel = t == ui.track;
            g.setColour(sel ? tint(c, 0.6f) : panel);
            g.fillRoundedRectangle(r, 10.0f);
            g.setColour(sel ? ink : edge);
            g.drawRoundedRectangle(r, 10.0f, sel ? 2.5f : 1.5f);
            g.setColour(engs[(size_t)t] ? c : well);
            g.fillRoundedRectangle(r.withWidth(12.0f).reduced(0.0f, 6.0f).translated(6.0f, 0.0f), 4.0f);
            g.setColour(ink);
            g.setFont(font(28.0f, 700));
            g.drawText(juce::String(t + 1), (int)r.getX() + 22, (int)r.getY(), 26, (int)r.getHeight(), juce::Justification::centred);
            g.setFont(font(23.0f, 700));
            g.drawFittedText(names[(size_t)t], (int)r.getX() + 54, (int)r.getY() + 8, (int)r.getWidth() - 110, 28,
                             juce::Justification::centredLeft, 1, 0.75f);
            g.setColour(soft);
            g.setFont(font(16.0f, 600));
            g.drawText("P" + juce::String(pats[(size_t)t] + 1), (int)r.getX() + 54, (int)r.getY() + 42, 40, 24,
                       juce::Justification::centredLeft);
            auto ch = changeRect(t);                 // (the instrument: a menu)
            g.setColour(panel.withAlpha(0.9f));
            g.fillRoundedRectangle(ch, 5.0f);
            g.setColour(edge);
            g.drawRoundedRectangle(ch, 5.0f, 1.2f);
            g.setColour(ink);
            g.setFont(font(15.0f, 700));
            g.drawText("CHANGE", ch.toNearestInt().withTrimmedRight(14), juce::Justification::centred);
            juce::Path tri;
            const float tx = ch.getRight() - 16.0f, ty = ch.getCentreY();
            tri.addTriangle(tx, ty - 3.0f, tx + 9.0f, ty - 3.0f, tx + 4.5f, ty + 3.5f);
            g.fillPath(tri);
            auto m = muteRect(t);                    // M: muted
            g.setColour(mutes[(size_t)t] ? tint(hot, 0.6f) : panel);
            g.fillRoundedRectangle(m, 6.0f);
            g.setColour(mutes[(size_t)t] ? ink : edge);
            g.drawRoundedRectangle(m, 6.0f, mutes[(size_t)t] ? 2.5f : 1.5f);
            g.setColour(ink);
            g.setFont(font(20.0f, 700));
            g.drawText("M", m.toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int t = (int)(e.position.y / rowH);
        if (t < 0 || t > 7)
            return;
        if (muteRect(t).contains(e.position)) {
            ui.core([&] { dnc_track_set_mute(t, !dnc_track_mute(t)); return 0; });
            poll();
            return;
        }
        if (e.mods.isPopupMenu() || changeRect(t).contains(e.position)) {
            instrumentMenu(t);
            return;
        }
        if (onPick)
            onPick(t);
    }
private:
    juce::Rectangle<float> muteRect(int t) const { return { (float)getWidth() - 50.0f, (float)(t * rowH) + 18.0f, 40.0f, 40.0f }; }
    juce::Rectangle<float> changeRect(int t) const { return { 96.0f, (float)(t * rowH) + 41.0f, 82.0f, 26.0f }; }
    void instrumentMenu(int t)
    {
        juce::PopupMenu m;
        m.addSectionHeader("TRACK " + juce::String(t + 1) + " PLAYS");
        ui.core([&] {
            const int cur = dnc_track_engine(t);
            for (int e = 0; e < dnc_engine_count(); ++e)
                m.addItem(e + 1, dnc_engine_name(e), true, e == cur);
            return 0;
        });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this, t](int r) {
            if (r <= 0)
                return;
            ui.core([&] { dnc_track_set_engine(t, r - 1); return 0; });
            if (onPick)
                onPick(t);
        });
    }
    Ui& ui;
    std::function<void(int)> onPick;
    std::array<juce::String, 8> names;
    std::array<uint32_t, 8> cols {};
    std::array<int, 8> mutes {}, pats {}, engs {};
    int shownSel = -1;
};

// ---------------------------------------------------------------------------------------------- the editor ---
struct DreadnoughtEditor::Root : public juce::Component
{
    Root(DreadnoughtProcessor& p) : ui { p }, top(ui), tracks(ui, [this](int t) { pick(t); }), sound(ui), pattern(ui), fx(ui), samples(ui)
    {
        ui.onStructure = [this] { current().rebuild(); };
        addAndMakeVisible(top);
        addAndMakeVisible(tracks);
        const char* const names[4] = { "SOUND", "PATTERN", "EFFECTS", "SAMPLES" };
        for (int i = 0; i < 4; ++i) {
            tabs[(size_t)i].setButtonText(names[i]);
            tabs[(size_t)i].onClick = [this, i] { setTab((Tab)i); };
            addAndMakeVisible(tabs[(size_t)i]);
        }
        for (Page* pg : { (Page*)&sound, (Page*)&pattern, (Page*)&fx, (Page*)&samples })
            addChildComponent(pg);
        ui.core([&] { ui.track = dnc_track(); return 0; });
        pick(ui.track);
        setTab(tabSound);
    }
    Page& current() { return tab == tabSound ? (Page&)sound : tab == tabPattern ? (Page&)pattern : tab == tabFx ? (Page&)fx : (Page&)samples; }
    void setTab(Tab t)
    {
        tab = t;
        for (int i = 0; i < 4; ++i)
            tabs[(size_t)i].setToggleState(i == (int)t, juce::dontSendNotification);
        sound.setVisible(t == tabSound);
        pattern.setVisible(t == tabPattern);
        fx.setVisible(t == tabFx);
        samples.setVisible(t == tabSamples);
        current().rebuild();
        repaint();
    }
    void pick(int t)
    {
        ui.track = t;
        ui.selIdx = {};
        ui.core([&] {
            dnc_track_select(t);
            ui.accent = rgb(dnc_track_colour(t));
            engine = dnc_track_engine(t);
            return 0;
        });
        tracks.poll();
        top.poll();
        current().rebuild();
        repaint();
    }
    void poll()
    {
        int e = 0;
        juce::Colour c;
        ui.core([&] {
            e = dnc_track_engine(ui.track);
            c = rgb(dnc_track_colour(ui.track));
            return 0;
        });
        if (e != engine || c != ui.accent || ui.proc.revision() != rev) {   // (an instrument changed; a project loaded)
            rev = ui.proc.revision();
            engine = e;
            ui.accent = c;
            current().rebuild();
            repaint();
        }
        top.poll();
        tracks.poll();
        current().poll();
    }
    void resized() override
    {
        top.setBounds(0, 0, getWidth(), 72);
        tracks.setBounds(16, 88, 230, TrackList::rowH * 8);
        for (int i = 0; i < 4; ++i)
            tabs[(size_t)i].setBounds(262 + i * 166, 88, 158, 46);
        const juce::Rectangle<int> page(262, 146, getWidth() - 262 - 16, getHeight() - 146 - 14);
        for (Page* pg : { (Page*)&sound, (Page*)&pattern, (Page*)&fx, (Page*)&samples })
            pg->setBounds(page);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(paper);
        g.setColour(tint(ui.accent, 0.5f));          // (the page's tab row in the track's colour)
        g.fillRoundedRectangle(juce::Rectangle<float>(258.0f, 84.0f, (float)getWidth() - 270.0f, 54.0f), 10.0f);
    }
    Ui ui;
    TopBar top;
    TrackList tracks;
    std::array<juce::TextButton, 4> tabs;
    SoundPage sound;
    PatternPage pattern;
    FxPage fx;
    SamplesPage samples;
    Tab tab = tabSound;
    int engine = -1;
    uint32_t rev = 0;
};

DreadnoughtEditor::DreadnoughtEditor(DreadnoughtProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&look);
    juce::LookAndFeel::setDefaultLookAndFeel(&look);
    root = std::make_unique<Root>(p);
    root->setBounds(0, 0, baseW, baseH);
    addAndMakeVisible(*root);
    setResizable(true, true);
    setResizeLimits(baseW * 3 / 5, baseH * 3 / 5, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)baseW / (double)baseH);
    setSize(baseW, baseH);
    startTimerHz(15);
}

DreadnoughtEditor::~DreadnoughtEditor()
{
    stopTimer();
    root.reset();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void DreadnoughtEditor::paint(juce::Graphics& g) { g.fillAll(paper); }

void DreadnoughtEditor::paintOverChildren(juce::Graphics& g)
{
    if (proc.active())
        return;
    g.setColour(paper.withAlpha(0.96f));             // (another DREADNOUGHT has the core)
    g.fillAll();
    g.setColour(ink);
    g.setFont(font(34.0f * (float)getWidth() / (float)baseW, 700));
    g.drawFittedText("ONE DREADNOUGHT AT A TIME FOR NOW\n\nAnother one is open in this project: use that one,\n"
                     "or close it and open this one again.",
                     getLocalBounds().reduced(60), juce::Justification::centred, 6);
}

void DreadnoughtEditor::resized()
{
    root->setTransform(juce::AffineTransform::scale((float)getWidth() / (float)baseW));   // (vectors: sharp at any size)
}

void DreadnoughtEditor::timerCallback()
{
    proc.idleTick();
    if (!proc.active())
        return;
    root->poll();
}

void DreadnoughtEditor::filesDropped(const juce::StringArray& files, int, int)
{
    root->setTab(tabSamples);
    root->samples.addFiles(files);
}

void DreadnoughtEditor::show(int track, Tab tab)
{
    root->pick(track);
    root->setTab(tab);
    refreshNow();
}

void DreadnoughtEditor::refreshNow()
{
    root->poll();
    root->resized();
}

dn::SamplesPage& DreadnoughtEditor::samplesPage() { return root->samples; }
