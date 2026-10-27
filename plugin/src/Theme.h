// SPDX-License-Identifier: GPL-3.0-only
// DREADNOUGHT plugin: the look. A light page, dark ink, big condensed type (Barlow Semi Condensed, as the manual and
// the web editor), each instrument in its BLOCKS colour (tools/manual/fm1_svg.py). Copyright (C) 2026 Jamie
// (TwistedGenetics).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace dn
{
const juce::Colour paper { 0xFFF4F3EE }, panel { 0xFFFFFFFF }, edge { 0xFFD3CEC2 }, ink { 0xFF101114 },
    soft { 0xFF55524A }, faint { 0xFFE7E4DB }, well { 0xFFEDEBE4 }, hot { 0xFFFF6A2B }, held { 0xFF1F5FBF },
    grid { 0xFFC9C4B8 }, bar { 0xFFE0DCD1 };

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
// the instrument's colour, deep enough to draw arcs and marks on white
inline juce::Colour deep(juce::Colour c) { return c.getPerceivedBrightness() > 0.62f ? c.darker(0.45f) : c; }

class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, faint);
        setColour(juce::PopupMenu::highlightedTextColourId, ink);
        setColour(juce::TextButton::buttonColourId, panel);
        setColour(juce::TextButton::buttonOnColourId, ink);
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, ink);
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
        g.setColour(on ? tint(hot, 0.45f) : down ? faint : over ? paper : panel);   // (on: a light fill, ink on it)
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(on ? ink : edge);
        g.drawRoundedRectangle(r, 6.0f, on ? 2.5f : 1.5f);
    }
};
}
