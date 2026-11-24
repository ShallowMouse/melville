#pragma once

// MELVILLE's own bar in the decade the theme is set to (PartHeader.h's Era): its lettering,
// its keys and the readouts of its counter and tempo, from drum counters and Nixie tubes to
// LEDs, vacuum fluorescent and LCD panels, and the flat screens since. STANDARD is the bar as
// it was before there was a choice (Eighties.h).

#include "PartHeader.h"
#include "Eighties.h"

namespace erabar
{
using partui::Era;

/** Small lettering (the tiles' names, captions, the status lines). */
inline juce::Font label (Era e, float size, bool bold = true)
{
    const int st = bold ? juce::Font::bold : juce::Font::plain;
    switch (e)
    {
        case Era::forties:
        case Era::fifties:   return partui::eraTypeface ("Georgia", "DejaVu Serif", size, st);
        case Era::sixties:   return partui::eraTypeface ("Arial", "Liberation Sans", size, st).withExtraKerningFactor (0.08f);
        case Era::nineties:  return partui::eraTypeface ("Verdana", "DejaVu Sans", size * 0.92f, st);
        case Era::noughties: return partui::eraTypeface ("Tahoma", "DejaVu Sans", size, st);
        case Era::tens:      return partui::eraTypeface ("Segoe UI", "Liberation Sans", size, juce::Font::plain);
        case Era::today:     return partui::eraTypeface (bold ? "Segoe UI Semibold" : "Segoe UI", "Liberation Sans", size, st);
        default:             return eighties::sans (size, bold);
    }
}

/** The bar's legends' colour. */
inline juce::Colour ink (Era e)
{
    switch (e)
    {
        case Era::forties:   return juce::Colour (0xffe9dcbc);     // ivory
        case Era::fifties:   return juce::Colour (0xfff0ecdf);
        case Era::seventies: return juce::Colour (0xffeedcb2);
        case Era::nineties:  return juce::Colour (0xffdfe4ee);
        case Era::tens:
        case Era::today:     return juce::Colour (0xfff2f3f5);
        default:             return eighties::legend();
    }
}
inline juce::Colour inkDim (Era e) { return ink (e).withAlpha (0.55f); }

/** MELVILLE's name on the bar. */
inline void drawWordmark (juce::Graphics& g, Era e, juce::Rectangle<float> r)
{
    auto mark = r.withSizeKeepingCentre (r.getWidth(), 44.0f);
    if (e == Era::standard || e == Era::eighties)
    {
        g.setColour (eighties::legend());
        g.setFont (eighties::sans (25.0f, true, true));
        g.drawText ("MELVILLE", mark.removeFromTop (28.0f), juce::Justification::bottomLeft);
        g.setColour (eighties::legendDim());
        g.setFont (eighties::sans (9.0f));
        g.drawText ("STUDIO  SYSTEM", mark.translated (1.0f, 0.0f), juce::Justification::topLeft);
        return;
    }
    const auto name = e == Era::tens || e == Era::today ? juce::String ("Melville") : juce::String ("MELVILLE");
    if (e == Era::forties)
    {
        // an engraved brass plate
        auto plate = mark.withHeight (30.0f).reduced (0.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8b866), 0.0f, plate.getY(), juce::Colour (0xff9a7832), 0.0f, plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 3.0f);
        g.setColour (juce::Colour (0xff5c4518));
        g.drawRoundedRectangle (plate.reduced (2.5f), 2.0f, 0.8f);
        g.setColour (juce::Colour (0xff2c2010));
        g.setFont (partui::eraNameFont (e, 24.0f));
        g.drawFittedText (name, plate.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour (inkDim (e));
        g.setFont (label (e, 9.0f, false));
        g.drawText ("STUDIO SYSTEM", mark.withTrimmedTop (31.0f), juce::Justification::centred);
        return;
    }
    g.setColour (e == Era::seventies ? juce::Colour (0xffffa53a) : e == Era::nineties ? juce::Colour (0xff5ef2d0)
                 : e == Era::noughties ? juce::Colour (0xff9fd0ff) : ink (e));
    g.setFont (partui::eraNameFont (e, 28.0f));
    g.drawFittedText (name, mark.removeFromTop (28.0f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.7f);
    g.setColour (inkDim (e));
    g.setFont (label (e, 9.0f, false));
    g.drawText (e == Era::tens || e == Era::today ? juce::String ("Studio system") : juce::String ("STUDIO  SYSTEM"),
                mark.translated (1.0f, 0.0f), juce::Justification::topLeft);
}

//==============================================================================
/** Digits on the drums of a mechanical counter (white on black, in a frame). */
inline void drawDrums (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour frame, juce::Font f)
{
    g.setColour (frame);
    g.fillRoundedRectangle (r, 3.0f);
    auto in = r.reduced (4.0f, 4.0f);
    int digits = 0; for (auto c : text) if (c != ':' && c != '.') ++digits;
    const float sepW = 6.0f, cellW = (in.getWidth() - sepW * (float) (text.length() - digits)) / (float) juce::jmax (1, digits);
    float x = in.getX();
    for (auto c : text)
    {
        if (c == ':') { x += sepW; continue; }
        if (c == '.')               // a decimal point, painted on the frame between the drums
        {
            g.setColour (juce::Colour (0xff1a1a1a));
            g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ x + sepW * 0.5f, in.getBottom() - 3.0f }));
            x += sepW;
            continue;
        }
        auto cell = juce::Rectangle<float> (x, in.getY(), cellW - 1.5f, in.getHeight());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050505), 0.0f, cell.getY(), juce::Colour (0xff2a2a2a), 0.0f, cell.getCentreY(), false));
        g.fillRect (cell.withHeight (cell.getHeight() * 0.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2a), 0.0f, cell.getCentreY(), juce::Colour (0xff050505), 0.0f, cell.getBottom(), false));
        g.fillRect (cell.withTrimmedTop (cell.getHeight() * 0.5f));
        g.setColour (juce::Colour (0xfff3efe4));
        g.setFont (f.withHeight (juce::jmin (cell.getHeight() * 0.8f, cell.getWidth() * 1.45f)));
        g.drawFittedText (juce::String::charToString (c), cell.toNearestInt(), juce::Justification::centred, 1, 0.5f);
        x += cellW;
    }
}

/** Nixie tubes: each digit glowing orange in its glass, the unlit numerals faintly behind. */
inline void drawNixies (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff0c0907));
    g.fillRoundedRectangle (r, 3.0f);
    auto in = r.reduced (3.0f, 3.0f);
    int digits = 0; for (auto c : text) if (c != ':' && c != '.') ++digits;
    const float sepW = 6.0f, cellW = (in.getWidth() - sepW * (float) (text.length() - digits)) / (float) juce::jmax (1, digits);
    float x = in.getX();
    const auto glow = juce::Colour (0xffff7a26);
    for (auto c : text)
    {
        if (c == ':' || c == '.')
        {
            g.setColour (glow);
            const float d = 2.6f;
            if (c == ':')
            {
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ x + sepW * 0.5f, in.getY() + in.getHeight() * 0.35f }));
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ x + sepW * 0.5f, in.getY() + in.getHeight() * 0.68f }));
            }
            else
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ x + sepW * 0.5f, in.getBottom() - 4.0f }));
            x += sepW;
            continue;
        }
        auto tube = juce::Rectangle<float> (x, in.getY(), cellW - 2.0f, in.getHeight());
        g.setColour (juce::Colour (0xff1d1410));
        g.fillRoundedRectangle (tube, tube.getWidth() * 0.45f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (tube, tube.getWidth() * 0.45f, 0.8f);
        const auto f = partui::eraTypeface ("Arial", "Liberation Sans", juce::jmin (tube.getHeight() * 0.78f, tube.getWidth() * 1.5f), juce::Font::plain);
        g.setFont (f);
        g.setColour (glow.withAlpha (0.08f));
        g.drawText ("8", tube, juce::Justification::centred, false);
        if (c != ' ')
        {
            g.setColour (glow.withAlpha (0.3f));
            for (auto d : { juce::Point<float> (-1, 0), juce::Point<float> (1, 0), juce::Point<float> (0, -1), juce::Point<float> (0, 1) })
                g.drawText (juce::String::charToString (c), tube.translated (d.x, d.y), juce::Justification::centred, false);
            g.setColour (juce::Colour (0xffffc08a));
            g.drawText (juce::String::charToString (c), tube, juce::Justification::centred, false);
        }
        x += cellW;
    }
}

/** A readout (the counter or the tempo) in the decade's display. */
inline void drawReadout (juce::Graphics& g, Era e, const juce::String& text, juce::Rectangle<float> r)
{
    switch (e)
    {
        case Era::forties:
            drawDrums (g, text, r, juce::Colour (0xffb8954a), partui::eraTypeface ("Georgia", "DejaVu Serif", 20.0f, juce::Font::bold));
            return;
        case Era::sixties:
            drawDrums (g, text, r, juce::Colour (0xffa9adb0), partui::eraTypeface ("Arial", "Liberation Sans", 20.0f, juce::Font::bold));
            return;
        case Era::fifties:
            drawNixies (g, text, r);
            return;
        case Era::seventies:       // amber gas-discharge segments
            eighties::drawLedWindow (g, r);
            eighties::drawSevenSegment (g, text, r.reduced (8.0f, 7.0f), juce::Colour (0xffff8a1e));
            return;
        case Era::nineties:        // vacuum fluorescent
        {
            g.setColour (juce::Colour (0xff04100e));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (juce::Colour (0xff5ef2d0).withAlpha (0.06f));
            g.fillRoundedRectangle (r.reduced (2.0f), 1.5f);
            eighties::drawSevenSegment (g, text, r.reduced (8.0f, 7.0f), juce::Colour (0xff6dfbd9));
            return;
        }
        case Era::noughties:       // a blue backlit LCD
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (r, 5.0f);
            auto lcd = r.reduced (2.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8cc6f2), 0.0f, lcd.getY(), juce::Colour (0xff4a8fd0), 0.0f, lcd.getBottom(), false));
            g.fillRoundedRectangle (lcd, 4.0f);
            eighties::drawSevenSegment (g, text, r.reduced (9.0f, 8.0f), juce::Colour (0xff0d2238));
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.3f), 0.0f, lcd.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, lcd.getCentreY(), false));
            g.fillRoundedRectangle (lcd.withHeight (lcd.getHeight() * 0.5f), 4.0f);
            return;
        }
        case Era::tens:
            g.setColour (juce::Colour (0xff141518));
            g.fillRect (r);
            g.setColour (ink (e));
            g.setFont (partui::eraTypeface ("Segoe UI Light", "Liberation Sans", r.getHeight() * 0.72f, juce::Font::plain));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            return;
        case Era::today:
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (ink (e));
            g.setFont (partui::eraTypeface ("Segoe UI Semibold", "Liberation Sans", r.getHeight() * 0.62f, juce::Font::bold));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            return;
        default:
            eighties::drawLedWindow (g, r);
            eighties::drawSevenSegment (g, text, r.reduced (8.0f, 7.0f), eighties::ledRed());
            return;
    }
}

/** A lamp on a tile (the part's colour): jewels and LEDs until the flat screens, a dot since. */
inline void drawLamp (juce::Graphics& g, Era e, juce::Point<float> c, bool on, juce::Colour colour)
{
    if (e == Era::tens || e == Era::today)
    {
        g.setColour (on ? colour : juce::Colours::white.withAlpha (0.12f));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
        return;
    }
    eighties::drawLed (g, c, 2.6f, on, colour);
}

/** The colour a transport key lights (lit in its colour, as the decade did it). */
enum class Role { wind, stop, play, rec };
inline juce::Colour roleColour (Role r)
{
    switch (r)
    {
        case Role::rec:  return juce::Colour (0xffd8432f);
        case Role::play: return juce::Colour (0xff4fb35a);
        case Role::stop: return juce::Colour (0xffe7e0cc);
        default:         return juce::Colour (0xff9a9a96);
    }
}

} // namespace erabar
