// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT plugin: the look. A light page, dark ink, big condensed type (Barlow Semi Condensed, as the manual and
// the web editor), each instrument in its BLOCKS colour (tools/manual/fm1_svg.py). Copyright (C) 2026 Jamie
// (TwistedGenetics).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace dn
{
// the FM-1's skins (src/app/dn_app.c dn_skin): BLOCKS (its default: a near-black ground, cream text, every value a
// tile of its own colour with dark text on it) and LIGHT (a warm white ground, ink text, an orange accent)
enum { skinBlocks, skinLight };
inline int skin = skinBlocks;
inline juce::Colour paper, panel, edge, ink, soft, faint, well, hot { 0xFFFF6A2B }, held, grid, bar, red, onChip { 0xFF101114 };
// the tiles' colours (DN_KCOL / DN_KTINT): a value's place in its row picks one (orange, yellow, green, blue)
inline const juce::Colour kcol[4] = { juce::Colour(0xFFFF6A2B), juce::Colour(0xFFFFC21A), juce::Colour(0xFF3DDC97), juce::Colour(0xFF5B9BFF) };
inline const juce::Colour ktint[4] = { juce::Colour(0xFFFFC3AA), juce::Colour(0xFFFFE59E), juce::Colour(0xFFACEFD2), juce::Colour(0xFFB8D3FF) };
inline void setSkin(int s)
{
    skin = s == skinLight ? skinLight : skinBlocks;
    if (skin == skinBlocks) {
        paper = juce::Colour(0xFF101114), panel = juce::Colour(0xFF1C1E23), edge = juce::Colour(0xFF3A3D45);
        ink = juce::Colour(0xFFF4F3EE), soft = juce::Colour(0xFFC2C5CC), faint = juce::Colour(0xFF2A2E35);
        well = juce::Colour(0xFF16181C), held = juce::Colour(0xFFF4F3EE), grid = juce::Colour(0xFF3A3D45);
        bar = juce::Colour(0xFF1C1E23), red = juce::Colour(0xFFFF5A4A);
    } else {
        paper = juce::Colour(0xFFF4F3EE), panel = juce::Colour(0xFFFFFFFF), edge = juce::Colour(0xFFD3CEC2);
        ink = juce::Colour(0xFF101114), soft = juce::Colour(0xFF4A4C55), faint = juce::Colour(0xFFE7E4DB);
        well = juce::Colour(0xFFEDEBE4), held = juce::Colour(0xFF1F5FBF), grid = juce::Colour(0xFFC9C4B8);
        bar = juce::Colour(0xFFE0DCD1), red = juce::Colour(0xFFC8261A);
    }
}
inline const bool skinReady = (setSkin(skinBlocks), true);

inline juce::Typeface::Ptr face(int w)
{
    static juce::Typeface::Ptr m = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiCondensedMedium_ttf,
                                                                            BinaryData::BarlowSemiCondensedMedium_ttfSize);
    static juce::Typeface::Ptr s = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiCondensedSemiBold_ttf,
                                                                            BinaryData::BarlowSemiCondensedSemiBold_ttfSize);
    static juce::Typeface::Ptr b = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowSemiCondensedBold_ttf,
                                                                            BinaryData::BarlowSemiCondensedBold_ttfSize);
    return w >= 700 ? b : w >= 600 ? s : m;
}
inline juce::Font font(float h, int weight = 600) { return juce::Font(juce::FontOptions(face(weight)).withHeight(h)); }
inline juce::Colour tint(juce::Colour c, float a = 0.30f) { return paper.interpolatedWith(c, a); }
// a fill with text on it (onChip, always dark): BLOCKS the colour itself, LIGHT a light tint of it; soft: paler
inline juce::Colour chip(juce::Colour c, bool soft_ = false)
{
    if (skin == skinBlocks)
        return soft_ ? c.interpolatedWith(juce::Colour(0xFFF4F3EE), 0.45f) : c;
    return juce::Colour(0xFFF4F3EE).interpolatedWith(c, soft_ ? 0.3f : 0.5f);
}
// the instrument's colour as a line or mark on the ground (on white: deep enough to see)
inline juce::Colour deep(juce::Colour c) { return skin == skinBlocks ? c : c.getPerceivedBrightness() > 0.62f ? c.darker(0.45f) : c; }

class Look : public juce::LookAndFeel_V4
{
public:
    Look() { apply(); }
    void apply()
    {
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, faint);
        setColour(juce::PopupMenu::highlightedTextColourId, ink);
        setColour(juce::ComboBox::backgroundColourId, panel);
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::ComboBox::arrowColourId, ink);
        setColour(juce::TextEditor::highlightedTextColourId, ink);
        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, ink);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, onChip);
        setColour(juce::ComboBox::outlineColourId, edge);
        setColour(juce::ToggleButton::textColourId, ink);
        setColour(juce::ToggleButton::tickColourId, ink);
        setColour(juce::ToggleButton::tickDisabledColourId, soft);
        setColour(juce::TextEditor::backgroundColourId, panel);
        setColour(juce::TextEditor::textColourId, ink);
        setColour(juce::TextEditor::outlineColourId, edge);
        setColour(juce::TextEditor::focusedOutlineColourId, ink);
        setColour(juce::TextEditor::highlightColourId, faint);
        setColour(juce::CaretComponent::caretColourId, ink);
        setColour(juce::ScrollBar::thumbColourId, grid);
        setColour(juce::Label::textColourId, ink);
        setColour(juce::AlertWindow::backgroundColourId, panel);
        setColour(juce::AlertWindow::textColourId, ink);
        setColour(juce::AlertWindow::outlineColourId, edge);
        setColour(juce::ResizableWindow::backgroundColourId, paper);
        setColour(juce::DocumentWindow::textColourId, ink);
        setDefaultSansSerifTypeface(face(600));
    }
    juce::Font getPopupMenuFont() override { return font(19.0f, 600); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(19.0f, 700); }
    juce::Font getLabelFont(juce::Label&) override { return font(19.0f, 700); }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return font(juce::jmin(19.0f, h * 0.6f), 700); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        const bool on = b.getToggleState();
        g.setColour(on ? chip(hot) : down ? faint : over ? faint.interpolatedWith(panel, 0.5f) : panel);   // (on: the accent)
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(on ? (skin == skinBlocks ? hot : ink) : edge);
        g.drawRoundedRectangle(r, 6.0f, on ? 2.5f : 1.5f);
    }
};
}
