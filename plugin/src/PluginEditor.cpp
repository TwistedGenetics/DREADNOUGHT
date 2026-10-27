// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT as a plugin: the editor. Copyright (C) 2026 Jamie (TwistedGenetics).
#include "PluginEditor.h"
#include "dn_core.h"

namespace
{
// a light panel, dark ink (big, high-contrast text)
const juce::Colour cBg { 0xFFE9E5DB }, cInk { 0xFF141418 }, cSoft { 0xFF4A473F }, cFace { 0xFFFBFAF6 },
    cEdge { 0xFF8E897D }, cDown { 0xFFD6D0C2 }, cLed { 0xFFFF4D1A }, cLedOff { 0xFFCDC8BB }, cBezel { 0xFF5B5850 },
    cWhite { 0xFFFFFFFF }, cBlack { 0xFF26262A }, cLitW { 0xFFFFA57A }, cLitB { 0xFFFF5E26 }, cDimW { 0xFFFFDCCB },
    cDimB { 0xFF8C4A30 }, cHeld { 0xFF1F5FBF };

juce::Font bold(float h) { return juce::Font(juce::FontOptions().withHeight(h).withStyle("Bold")); }

enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP };
enum { E_SELECT, E_ALGO, E_PRESET, E_K1, E_K2, E_K3, E_K4 };
constexpr int nKeys = 27;
inline bool isBlack(int k)
{
    const int n = (53 + k) % 12;                    // (key 0 is F3)
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}
// the computer's keys: the bottom row F3..E4, the top row F4..G5 (as the web editor's)
const char* const kbLow = "zsxdcfvbhnjm";
const char* const kbHigh = "q2w3e4rt6y7ui9o";
int kbKey(juce::juce_wchar c)
{
    c = juce::CharacterFunctions::toLowerCase(c);
    for (int i = 0; kbLow[i]; ++i)
        if (c == (juce::juce_wchar)kbLow[i])
            return i;
    for (int i = 0; kbHigh[i]; ++i)
        if (c == (juce::juce_wchar)kbHigh[i])
            return 12 + i;
    return -1;
}
}

// ---------------------------------------------------------------------------------------------- the controls ---
struct Encoder : juce::Component
{
    juce::String label;
    std::function<void(int)> onTurn;
    float angle = 0.0f, acc = 0.0f, wheelAcc = 0.0f;
    int lastY = 0;
    bool big = true;

    void turn(int d)
    {
        if (d == 0)
            return;
        angle += 0.2618f * (float)d;               // (a detent: 15 degrees, as the FM-1's encoders click)
        if (onTurn)
            onTurn(d);
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        const float labelH = 24.0f;
        auto r = getLocalBounds().toFloat();
        const float d = juce::jmin(r.getWidth(), r.getHeight() - labelH - 6.0f) - 8.0f;
        const juce::Rectangle<float> k(r.getCentreX() - d / 2.0f, 4.0f, d, d);
        g.setColour(cEdge);
        g.fillEllipse(k.expanded(3.0f));
        g.setColour(cFace);
        g.fillEllipse(k);
        g.setColour(cEdge.withAlpha(0.5f));
        for (int i = 0; i < 24; ++i) {             // (the knurls)
            const float a = (float)i * juce::MathConstants<float>::twoPi / 24.0f + angle;
            g.drawLine(k.getCentreX() + std::sin(a) * d * 0.40f, k.getCentreY() - std::cos(a) * d * 0.40f,
                       k.getCentreX() + std::sin(a) * d * 0.47f, k.getCentreY() - std::cos(a) * d * 0.47f, 1.5f);
        }
        g.setColour(cInk);
        g.drawLine(k.getCentreX() + std::sin(angle) * d * 0.08f, k.getCentreY() - std::cos(angle) * d * 0.08f,
                   k.getCentreX() + std::sin(angle) * d * 0.36f, k.getCentreY() - std::cos(angle) * d * 0.36f, 4.0f);
        g.setFont(bold(big ? 18.0f : 17.0f));
        g.drawText(label, getLocalBounds().removeFromBottom((int)labelH), juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        lastY = e.y;
        acc = 0.0f;
    }
    void mouseDrag(const juce::MouseEvent& e) override   // (8 px a detent; shift: 4 at a time)
    {
        acc += (float)(lastY - e.y) / 8.0f;
        lastY = e.y;
        const int n = (int)acc;
        if (n != 0) {
            acc -= (float)n;
            turn(n * (e.mods.isShiftDown() ? 4 : 1));
        }
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float dy = w.isReversed ? -w.deltaY : w.deltaY;
        int n;
        if (!w.isSmooth) {
            n = dy > 0.0f ? 1 : dy < 0.0f ? -1 : 0;
        } else {
            wheelAcc += dy * 16.0f;
            n = (int)wheelAcc;
            wheelAcc -= (float)n;
        }
        turn(n * (e.mods.isShiftDown() ? 4 : 1));
    }
};

struct PanelButton : juce::Component
{
    juce::String label;
    std::function<void(bool)> press;
    bool down = false, latched = false, lit = false;

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(down || latched ? cDown : cFace);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(latched ? cHeld : cEdge);
        g.drawRoundedRectangle(r, 8.0f, latched ? 4.0f : 2.0f);
        const float led = 12.0f;
        g.setColour(lit ? cLed : cLedOff);
        g.fillEllipse(r.getCentreX() - led / 2.0f, r.getY() + 9.0f, led, led);
        g.setColour(cInk);
        g.setFont(bold(label.length() > 5 ? 14.0f : 16.0f));
        g.drawFittedText(label, r.withTrimmedTop(24.0f).toNearestInt(), juce::Justification::centred, 2);
    }
    void mouseDown(const juce::MouseEvent& e) override   // shift-click / right-click: held down (SAVE + FX ...)
    {
        if (e.mods.isShiftDown() || e.mods.isPopupMenu()) {
            latched = !latched;
            press(latched);
        } else if (latched) {
            latched = false;
            press(false);
        } else {
            down = true;
            press(true);
        }
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (down) {
            down = false;
            press(false);
            repaint();
        }
    }
};

struct Keys : juce::Component
{
    std::function<void(int, bool)> press;
    uint32_t lit = 0, dim = 0, held = 0;
    int mouseKey = -1;

    int whites() const { return 16; }
    juce::Rectangle<float> keyRect(int k) const
    {
        const float ww = (float)getWidth() / (float)whites(), h = (float)getHeight();
        int wx = 0;
        for (int i = 0; i < k; ++i)
            wx += isBlack(i) ? 0 : 1;
        if (!isBlack(k))
            return { (float)wx * ww, 0.0f, ww, h };
        return { (float)wx * ww - ww * 0.31f, 0.0f, ww * 0.62f, h * 0.60f };
    }
    int keyAt(juce::Point<float> p) const
    {
        for (int k = 0; k < nKeys; ++k)
            if (isBlack(k) && keyRect(k).contains(p))
                return k;
        for (int k = 0; k < nKeys; ++k)
            if (!isBlack(k) && keyRect(k).contains(p))
                return k;
        return -1;
    }
    void paint(juce::Graphics& g) override
    {
        for (int pass = 0; pass < 2; ++pass)
            for (int k = 0; k < nKeys; ++k) {
                if (isBlack(k) != (pass == 1))
                    continue;
                const auto r = keyRect(k).reduced(pass ? 0.0f : 1.5f, 0.0f);
                const bool on = ((lit >> k) & 1u) != 0, dm = ((dim >> k) & 1u) != 0, h = ((held >> k) & 1u) != 0;
                juce::Colour c = pass ? (on ? cLitB : dm ? cDimB : cBlack) : (on ? cLitW : dm ? cDimW : cWhite);
                if (h)
                    c = c.interpolatedWith(cDown, 0.5f);
                g.setColour(c);
                g.fillRoundedRectangle(r, 5.0f);
                g.setColour(cBezel);
                g.drawRoundedRectangle(r, 5.0f, 1.5f);
            }
    }
    void setHeld(int k, bool on)
    {
        if (k < 0)
            return;
        held = on ? held | 1u << k : held & ~(1u << k);
        press(k, on);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        mouseKey = keyAt(e.position);
        setHeld(mouseKey, true);
    }
    void mouseDrag(const juce::MouseEvent& e) override   // (a glide along the keys)
    {
        const int k = keyAt(e.position);
        if (k != mouseKey) {
            setHeld(mouseKey, false);
            mouseKey = k;
            setHeld(mouseKey, true);
        }
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        setHeld(mouseKey, false);
        mouseKey = -1;
    }
};

struct ScreenView : juce::Component
{
    juce::Image img { juce::Image::ARGB, (int)DNC_W, (int)DNC_H, true };
    uint32_t serial = 0xFFFFFFFFu;
    void paint(juce::Graphics& g) override
    {
        g.setColour(cBezel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 10.0f);
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);   // (the pixels, sharp)
        g.drawImage(img, getLocalBounds().reduced(8).toFloat(), juce::RectanglePlacement::stretchToFit);
    }
};

// ---------------------------------------------------------------------------------------------------- the panel ---
struct DreadnoughtEditor::Panel : juce::Component
{
    DreadnoughtProcessor& proc;
    ScreenView screen;
    std::array<Encoder, 7> enc;
    std::array<PanelButton, 14> btn;
    Keys keys;
    juce::ToggleButton sync { "FOLLOW THE HOST (play, stop, tempo)" };

    explicit Panel(DreadnoughtProcessor& p) : proc(p)
    {
        addAndMakeVisible(screen);
        const char* const encNames[7] = { "SELECT", "ALGORITHM", "PRESETS", "K1", "K2", "K3", "K4" };
        for (int i = 0; i < 7; ++i) {
            enc[(size_t)i].label = encNames[i];
            enc[(size_t)i].big = i < 3;
            enc[(size_t)i].onTurn = [this, i](int d) { proc.post(DreadnoughtProcessor::evTurn, i, d); };
            addAndMakeVisible(enc[(size_t)i]);
        }
        const char* const btnNames[14] = { "FX",  "SEL",  "ENV", "LFO", "EDIT", "GLO",       "HOME",
                                           "SAVE", "ARP", "SEQ", "PLAY / STOP", "REC", "OCT -", "OCT +" };
        for (int i = 0; i < 14; ++i) {
            btn[(size_t)i].label = btnNames[i];
            btn[(size_t)i].press = [this, i](bool on) { proc.post(DreadnoughtProcessor::evButton, i, on ? 1 : 0); };
            addAndMakeVisible(btn[(size_t)i]);
        }
        keys.press = [this](int k, bool on) { proc.post(DreadnoughtProcessor::evKey, k, on ? 1 : 0); };
        addAndMakeVisible(keys);
        sync.setToggleState(proc.syncToHost.load(), juce::dontSendNotification);
        sync.setColour(juce::ToggleButton::textColourId, cInk);
        sync.setColour(juce::ToggleButton::tickColourId, cInk);
        sync.setColour(juce::ToggleButton::tickDisabledColourId, cSoft);
        sync.onClick = [this] { proc.syncToHost = sync.getToggleState(); };
        addAndMakeVisible(sync);
    }

    void resized() override
    {
        // the left: SELECT, PRESETS, ALGORITHM and OCT (as the FM-1)
        enc[E_SELECT].setBounds(28, 70, 172, 132);
        enc[E_PRESET].setBounds(28, 206, 172, 132);
        enc[E_ALGO].setBounds(28, 342, 172, 132);
        btn[B_OCTDN].setBounds(28, 486, 84, 68);
        btn[B_OCTUP].setBounds(116, 486, 84, 68);
        screen.setBounds(220, 66, 496, 496);        // (240 x 240, twice the size, in its bezel)
        for (int k = 0; k < 4; ++k)
            enc[(size_t)(E_K1 + k)].setBounds(740 + k * 89, 70, 87, 118);
        for (int i = 0; i < 6; ++i) {
            btn[(size_t)i].setBounds(740 + i * 59, 214, 57, 76);
            btn[(size_t)(6 + i)].setBounds(740 + i * 59, 296, 57, 76);
        }
        sync.setBounds(736, 392, 356, 30);
        keys.setBounds(28, 584, 1056, 124);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(cBg);
        g.setColour(cInk);
        g.setFont(bold(26.0f));
        g.drawText("DREADNOUGHT", 28, 14, 300, 34, juce::Justification::centredLeft);
        g.setColour(cSoft);
        g.setFont(bold(17.0f));
        g.drawText("v" DN_VERSION_STR "   jungle / drum & bass groovebox", 238, 16, 480, 30,
                   juce::Justification::centredLeft);
        g.setColour(cInk);
        g.setFont(bold(15.0f));
        juce::String help;
        help << "MIDI CHANNELS 1-8: TRACKS 1-8\n"
             << "CHANNELS 9-16: THE TRACK SHOWN\n"
             << "BREAKS / PADS: C1 = THE FIRST SLICE\n\n"
             << "KEYS: Z S X D C ... / Q 2 W 3 E ...\n"
             << "KNOBS: DRAG OR SCROLL (SHIFT: x4)\n"
             << "SHIFT-CLICK A BUTTON: HOLD IT DOWN\n"
             << "(SAVE + FX, HOME + SELECT ...)";
        g.drawFittedText(help, 740, 430, 352, 136, juce::Justification::topLeft, 8, 1.0f);
        if (!proc.active()) {
            g.setColour(cBg.withAlpha(0.94f));
            g.fillRect(getLocalBounds());
            g.setColour(cInk);
            g.setFont(bold(30.0f));
            g.drawFittedText("ONE DREADNOUGHT AT A TIME FOR NOW\n\nAnother one is open in this project: use that one,\n"
                             "or close it and open this again.",
                             getLocalBounds().reduced(80), juce::Justification::centred, 6);
        }
    }

    void update(bool force)
    {
        if (proc.screen(screen.serial, screen.img) || force)
            screen.repaint();
        const uint32_t b = proc.ledsButtons();
        for (int i = 0; i < 14; ++i) {
            const bool on = ((b >> i) & 1u) != 0;
            if (on != btn[(size_t)i].lit) {
                btn[(size_t)i].lit = on;
                btn[(size_t)i].repaint();
            }
        }
        const uint32_t kl = proc.ledsKeys(), kd = proc.ledsKeysDim();
        if (kl != keys.lit || kd != keys.dim) {
            keys.lit = kl;
            keys.dim = kd;
            keys.repaint();
        }
        if (sync.getToggleState() != proc.syncToHost.load())
            sync.setToggleState(proc.syncToHost.load(), juce::dontSendNotification);
    }
};

// --------------------------------------------------------------------------------------------------- the editor ---
DreadnoughtEditor::DreadnoughtEditor(DreadnoughtProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    panel = std::make_unique<Panel>(p);
    addAndMakeVisible(*panel);
    panel->setBounds(0, 0, baseW, baseH);
    panel->addMouseListener(this, true);
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)baseW / (double)baseH);
    setSize(baseW, baseH);
    refreshNow();
    startTimerHz(30);
}

DreadnoughtEditor::~DreadnoughtEditor()
{
    stopTimer();
    proc.post(DreadnoughtProcessor::evAllUp, 0, 0);
}

void DreadnoughtEditor::paint(juce::Graphics& g) { g.fillAll(cBg); }

void DreadnoughtEditor::resized()
{
    const float s = (float)getWidth() / (float)baseW;
    panel->setTransform(juce::AffineTransform::scale(s));
}

void DreadnoughtEditor::refreshNow() { panel->update(true); }

void DreadnoughtEditor::timerCallback()
{
    proc.idleTick();
    panel->update(false);
}

bool DreadnoughtEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
        return false;
    return kbKey(key.getTextCharacter()) >= 0;      // (the keys play in keyStateChanged: downs and ups)
}

bool DreadnoughtEditor::keyStateChanged(bool)
{
    bool used = false;
    auto check = [&](const char* row, int base) {
        for (int i = 0; row[i]; ++i) {
            const int k = base + i;
            const int c = row[i];
            const bool down = juce::KeyPress::isKeyCurrentlyDown(c) ||
                              juce::KeyPress::isKeyCurrentlyDown(juce::CharacterFunctions::toUpperCase((juce::juce_wchar)c));
            if (down != kbDown[(size_t)k]) {
                kbDown[(size_t)k] = down;
                panel->keys.held = down ? panel->keys.held | 1u << k : panel->keys.held & ~(1u << k);
                proc.post(DreadnoughtProcessor::evKey, k, down ? 1 : 0);
                panel->keys.repaint();
                used = true;
            }
        }
    };
    check(kbLow, 0);
    check(kbHigh, 12);
    return used;
}

void DreadnoughtEditor::focusLost(FocusChangeType)
{
    for (int k = 0; k < nKeys; ++k)
        if (kbDown[(size_t)k]) {
            kbDown[(size_t)k] = false;
            panel->keys.held &= ~(1u << k);
            proc.post(DreadnoughtProcessor::evKey, k, 0);
        }
    panel->keys.repaint();
}
