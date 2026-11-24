#pragma once

// MELVILLE's own bar in the decade the theme is set to (PartHeader.h's Era): its lettering,
// its keys and the readouts of its counter and tempo, from drum counters and Nixie tubes to
// LEDs, vacuum fluorescent and LCD panels, and the flat screens since. STANDARD is the bar as
// (The 1980s keep the early-80s bar of Eighties.h, in MELVILLE's blue.)

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
    if (e == Era::eighties)
    {
        g.setColour (partui::melvilleBlue().brighter (0.3f));
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
        // an enamelled plate in MELVILLE's blue, in a brass rim, the name in cream
        auto plate = mark.withHeight (30.0f).reduced (0.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8b866), 0.0f, plate.getY(), juce::Colour (0xff9a7832), 0.0f, plate.getBottom(), false));
        g.fillRoundedRectangle (plate, 3.0f);
        const auto b = partui::melvilleBlue();
        g.setGradientFill (juce::ColourGradient (b.darker (0.15f), 0.0f, plate.getY(), b.darker (0.6f), 0.0f, plate.getBottom(), false));
        g.fillRoundedRectangle (plate.reduced (2.0f), 2.0f);
        g.setColour (juce::Colour (0xfff1e8cf));
        g.setFont (partui::eraNameFont (e, 24.0f));
        g.drawFittedText (name, plate.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        g.setColour (inkDim (e));
        g.setFont (label (e, 9.0f, false));
        g.drawText ("STUDIO SYSTEM", mark.withTrimmedTop (31.0f), juce::Justification::centred);
        return;
    }
    // the name in MELVILLE's blue, in the decade's way
    const auto blue = partui::melvilleBlue();
    g.setColour (e == Era::nineties || e == Era::seventies ? blue.brighter (0.45f) : e == Era::noughties ? blue.brighter (0.7f)
                 : e == Era::sixties || e == Era::fifties ? blue.brighter (0.3f) : blue.brighter (0.15f));
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
    g.setColour (juce::Colour (0xff070b0b));
    g.fillRoundedRectangle (r, 3.0f);
    auto in = r.reduced (3.0f, 3.0f);
    int digits = 0; for (auto c : text) if (c != ':' && c != '.') ++digits;
    const float sepW = 6.0f, cellW = (in.getWidth() - sepW * (float) (text.length() - digits)) / (float) juce::jmax (1, digits);
    float x = in.getX();
    const auto glow = partui::melvilleBlue().brighter (0.3f);      // (in MELVILLE's blue)
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
        g.setColour (juce::Colour (0xff0f1a19));
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
            g.setColour (glow.brighter (0.9f));
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
            drawDrums (g, text, r, partui::melvilleBlue().darker (0.35f), partui::eraTypeface ("Georgia", "DejaVu Serif", 20.0f, juce::Font::bold));
            return;
        case Era::sixties:
            drawDrums (g, text, r, juce::Colour (0xffa6b2b0), partui::eraTypeface ("Arial", "Liberation Sans", 20.0f, juce::Font::bold));
            return;
        case Era::fifties:
            drawNixies (g, text, r);
            return;
        case Era::seventies:       // gas-discharge segments, glowing in MELVILLE's blue
            eighties::drawLedWindow (g, r);
            eighties::drawSevenSegment (g, text, r.reduced (8.0f, 7.0f), partui::melvilleBlue().brighter (0.35f));
            return;
        case Era::nineties:        // vacuum fluorescent
        {
            g.setColour (juce::Colour (0xff04100e));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (partui::melvilleBlue().withAlpha (0.08f));
            g.fillRoundedRectangle (r.reduced (2.0f), 1.5f);
            eighties::drawSevenSegment (g, text, r.reduced (8.0f, 7.0f), partui::melvilleBlue().brighter (0.75f));
            return;
        }
        case Era::noughties:       // a blue backlit LCD
        {
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.fillRoundedRectangle (r, 5.0f);
            auto lcd = r.reduced (2.0f);
            g.setGradientFill (juce::ColourGradient (partui::melvilleBlue().brighter (0.75f), 0.0f, lcd.getY(), partui::melvilleBlue(), 0.0f, lcd.getBottom(), false));
            g.fillRoundedRectangle (lcd, 4.0f);
            eighties::drawSevenSegment (g, text, r.reduced (9.0f, 8.0f), juce::Colour (0xff0a2422));
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.3f), 0.0f, lcd.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, lcd.getCentreY(), false));
            g.fillRoundedRectangle (lcd.withHeight (lcd.getHeight() * 0.5f), 4.0f);
            return;
        }
        case Era::tens:
            g.setColour (juce::Colour (0xff121a1a));
            g.fillRect (r);
            g.setColour (partui::melvilleBlue().brighter (0.4f));
            g.setFont (partui::eraTypeface ("Segoe UI Light", "Liberation Sans", r.getHeight() * 0.72f, juce::Font::plain));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            return;
        case Era::today:
            g.setColour (partui::melvilleBlue().withAlpha (0.12f));
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (ink (e));
            g.setFont (partui::eraTypeface ("Segoe UI Semibold", "Liberation Sans", r.getHeight() * 0.62f, juce::Font::bold));
            g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
            return;
        case Era::eighties:        // LEDs, in MELVILLE's blue
            eighties::drawLedWindow (g, r);
            eighties::drawSevenSegment (g, text, r.reduced (8.0f, 7.0f), partui::melvilleBlue().brighter (0.5f));
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

//==============================================================================
/** The colours the bar's own icons (LIBRARY, SHEET, CLOCK, PATCHBAY, LINK, INTERFACE, UPDATE)
    are drawn in, in the decade's dress: a body, its lines, and the detail in MELVILLE's blue. */
struct IconColours { juce::Colour body, line, detail; };

inline IconColours iconColours (Era e)
{
    const auto b = partui::melvilleBlue();
    switch (e)
    {
        case Era::forties:   return { juce::Colour (0xffd9cba6), juce::Colour (0xff3b2c1b), b.darker (0.45f) };      // on a cream enamel badge
        case Era::fifties:   return { juce::Colour (0xff2b3633), juce::Colour (0xffcfd5d3), b.brighter (0.25f) };    // in a chrome bezel
        case Era::sixties:   return { juce::Colour (0xff151819), juce::Colour (0xffc9cfcd), b.brighter (0.2f) };     // an engraved black plate
        case Era::seventies: return { juce::Colour (0xff1b2221), juce::Colour (0xffe6d7b0), b.brighter (0.3f) };
        case Era::eighties:  return { juce::Colour (0xff9aa29f), juce::Colour (0xff1c2020), b.darker (0.35f) };      // a grey keycap
        case Era::nineties:  return { juce::Colour (0xff2c3d3e), juce::Colour (0xffc6d3d2), b.brighter (0.7f) };     // soft rubber
        case Era::noughties: return { b.brighter (0.2f), juce::Colours::white, juce::Colours::white };               // a glossy icon
        case Era::tens:      return { b.darker (0.1f), juce::Colours::white.withAlpha (0.85f), juce::Colours::white };   // flat
        case Era::today:     return { b.withAlpha (0.14f), juce::Colour (0xffdfe6e5), b.brighter (0.45f) };
    }
    return { juce::Colour (0xff2b2f33), juce::Colour (0xff8f8a80), b };
}

/** The badge an icon sits on (nothing for STANDARD); returns where the icon goes. */
inline juce::Rectangle<float> drawIconBadge (juce::Graphics& g, Era e, juce::Rectangle<float> r)
{
    const auto b = partui::melvilleBlue();
    const auto ic = iconColours (e);
    switch (e)
    {
        case Era::forties:
            g.setColour (juce::Colour (0xffa9843c));                    // brass rim
            g.fillRoundedRectangle (r, 8.0f);
            g.setGradientFill (juce::ColourGradient (ic.body.brighter (0.1f), 0.0f, r.getY(), ic.body.darker (0.15f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r.reduced (2.5f), 6.5f);
            return r.reduced (5.0f);
        case Era::fifties:
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeef0f0), 0.0f, r.getY(), juce::Colour (0xff7d8283), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (ic.body);
            g.fillRoundedRectangle (r.reduced (3.0f), 4.0f);
            return r.reduced (5.0f);
        case Era::sixties:
            g.setColour (ic.body);
            g.fillRect (r);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawRect (r, 1.0f);
            return r.reduced (4.0f);
        case Era::seventies:
            g.setColour (ic.body);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (b.withMultipliedBrightness (0.6f));
            g.fillRect (r.removeFromBottom (3.0f).reduced (6.0f, 0.0f));
            return r.reduced (4.0f);
        case Era::eighties:
            eighties::drawKey (g, r, ic.body, false, false);
            return r.reduced (5.0f);
        case Era::nineties:
            g.setColour (juce::Colour (0xff0b1011));
            g.fillRoundedRectangle (r, 10.0f);
            g.setGradientFill (juce::ColourGradient (ic.body.brighter (0.1f), 0.0f, r.getY(), ic.body.darker (0.25f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r.reduced (1.5f), 9.0f);
            return r.reduced (5.0f);
        case Era::noughties:
        {
            g.setGradientFill (juce::ColourGradient (ic.body, 0.0f, r.getY(), b.darker (0.45f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 9.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.45f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.05f), 0.0f, r.getCentreY(), false));
            g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.5f).reduced (2.0f, 1.5f), 7.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (r, 9.0f, 1.0f);
            return r.reduced (5.0f);
        }
        case Era::tens:
            g.setColour (ic.body);
            g.fillRect (r);
            return r.reduced (5.0f);
        case Era::today:
            g.setColour (ic.body);
            g.fillRoundedRectangle (r, r.getWidth() * 0.26f);
            return r.reduced (5.0f);
    }
    return r;
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
