#pragma once

// MELVILLE's look: PEQUOD's charcoal and Courier Prime, with a verdigris accent (the
// colour of old ship's brass) so it reads as the hub rather than one of the parts.

#include <juce_gui_basics/juce_gui_basics.h>
#include "Hardware.h"
#include "BinaryData.h"

namespace melville
{

namespace Colours
{
    const juce::Colour bg       { 0xff17191b };
    const juce::Colour panel    { 0xff212427 };
    const juce::Colour panelHi  { 0xff2b2f33 };
    const juce::Colour line     { 0xff3a3d40 };
    const juce::Colour accent   { 0xff5fa8a0 };     // verdigris
    const juce::Colour accentLo { 0xff2f5753 };
    const juce::Colour text     { 0xffede6d6 };
    const juce::Colour textDim  { 0xff8f8a80 };
    const juce::Colour lampOff  { 0xff3a3a3a };
    const juce::Colour warn     { 0xffc0503a };

    // each part's own accent, for its button and its block in the patchbay
    inline juce::Colour forPart (const juce::String& id)
    {
        if (id == "hawthorne") return juce::Colour (0xffe0a82e);   // mustard
        if (id == "emerson")   return juce::Colour (0xffe6d8b0);   // cream
        if (id == "whitman")   return juce::Colour (0xff7fb0d6);   // ink blue
        if (id == "thoreau")   return juce::Colour (0xff9fbf62);   // moss green
        if (id.startsWith ("pequod")) return juce::Colour (0xffa8443a);   // IRVING (each of them): oxblood
        if (id == "twain")     return juce::Colour (0xffd9875a);   // riverboat red-orange: the control room
        if (id == "alcott")    return juce::Colour (0xffb27fc0);   // plum: the instrument rack
        if (id == "midi")      return juce::Colour (0xff3a3936);   // charcoal: a MIDI patch bay of the early eighties
        if (id == "interface") return juce::Colour (0xffbdbdbd);
        if (id == "network")   return juce::Colour (0xff8fa6bd);   // steel blue
        return accent;                                             // the links
    }
}

class MelvilleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MelvilleLookAndFeel()
    {
        mono = juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeRegular_ttf, BinaryData::CourierPrimeRegular_ttfSize);
        monoBold = juce::Typeface::createSystemTypefaceFor (BinaryData::CourierPrimeBold_ttf, BinaryData::CourierPrimeBold_ttfSize);

        setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
        setColour (juce::TextButton::buttonColourId, Colours::panelHi);
        setColour (juce::TextButton::buttonOnColourId, Colours::accentLo);
        setColour (juce::TextButton::textColourOffId, Colours::text);
        setColour (juce::TextButton::textColourOnId, Colours::text);
        setColour (juce::ComboBox::backgroundColourId, Colours::panelHi);
        setColour (juce::ComboBox::textColourId, Colours::text);
        setColour (juce::ComboBox::outlineColourId, Colours::line);
        setColour (juce::ComboBox::arrowColourId, Colours::accent);
        setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accentLo);
        setColour (juce::PopupMenu::textColourId, Colours::text);
        setColour (juce::Label::textColourId, Colours::text);
        setColour (juce::ScrollBar::thumbColourId, Colours::line);
        setColour (juce::TooltipWindow::backgroundColourId, Colours::panelHi);
        setColour (juce::TooltipWindow::textColourId, Colours::text);
        setColour (juce::DocumentWindow::textColourId, Colours::text);
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        return f.isBold() ? monoBold : mono;
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) h * 0.5f), juce::Font::bold));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour, bool over, bool down) override
    {
        // a key standing proud of the panel; a latched one lights up
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        Pequod::hw::key (g, r, over && ! down ? bgColour.brighter (0.08f) : bgColour, down, b.getToggleState(),
                         b.findColour (juce::TextButton::buttonOnColourId).brighter (0.15f));
        if (! b.isEnabled())
        {
            g.setColour (Colours::bg.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 2.5f);
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
            return juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        // a fader: a slot with a scale, and a metal cap with a line across it in MELVILLE's colour
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        Pequod::hw::slot (g, r.withSizeKeepingCentre (r.getWidth(), 4.0f));
        g.setColour (Colours::textDim.withAlpha (0.5f));
        for (int k = 0; k <= 10; ++k)
        {
            const float tx = r.getX() + r.getWidth() * (float) k / 10.0f;
            const float len = k % 5 == 0 ? 5.0f : 3.0f;
            g.drawVerticalLine (juce::roundToInt (tx), r.getCentreY() - 6.0f - len, r.getCentreY() - 6.0f);
        }
        auto cap = juce::Rectangle<float> (12.0f, juce::jmin (22.0f, r.getHeight())).withCentre ({ pos, r.getCentreY() });
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (cap.translated (1.0f, 2.0f), 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd0d0cc), cap.getX(), cap.getY(), juce::Colour (0xff5a5a58), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (0xff202020));
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth(), 2.0f));
        g.setColour (Colours::accent);
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 4.0f, 1.0f));
    }

private:
    juce::Typeface::Ptr mono, monoBold;
};

} // namespace melville
