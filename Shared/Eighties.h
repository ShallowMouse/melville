#pragma once

// The early-80s drum machine and sequencer look, for MELVILLE's bar and POE: charcoal
// panels with white sans-serif legends, a band of red, orange, yellow and cream, square
// keys in those colours with a red LED, and red seven-segment LED readouts.

#include <juce_gui_basics/juce_gui_basics.h>

namespace eighties
{

// the band's four colours, and the panel
inline juce::Colour red()      { return juce::Colour (0xffd8432f); }
inline juce::Colour orange()   { return juce::Colour (0xffea7f2a); }
inline juce::Colour yellow()   { return juce::Colour (0xffe9c53b); }
inline juce::Colour cream()    { return juce::Colour (0xffe7e0cc); }
inline juce::Colour panel()    { return juce::Colour (0xff2b2b2c); }
inline juce::Colour panelLo()  { return juce::Colour (0xff1f1f20); }
inline juce::Colour legend()   { return juce::Colour (0xffeeeae0); }
inline juce::Colour legendDim(){ return juce::Colour (0xff9a968d); }
inline juce::Colour ledRed()   { return juce::Colour (0xffff3a24); }

inline juce::Colour band (int i)           // red, orange, yellow, cream, again: one per group of four steps
{
    switch (((i % 4) + 4) % 4) { case 0: return red(); case 1: return orange(); case 2: return yellow(); default: return cream(); }
}

/** The legends' type: a bold grotesque, as on the panels of the time. */
inline juce::Font sans (float size, bool bold = true, bool italic = false)
{
    int style = (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0);
   #if JUCE_WINDOWS || JUCE_MAC
    const juce::String family ("Arial");
   #else
    const juce::String family ("Liberation Sans");       // (Arial's metrics, on Linux)
   #endif
    return juce::Font (juce::FontOptions (family, size, style)).withExtraKerningFactor (0.04f);
}

/** The panel: flat charcoal, a hairline lighter along the top edge. */
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = 2.0f)
{
    g.setGradientFill (juce::ColourGradient (panel(), r.getX(), r.getY(), panelLo(), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (r, corner, 1.0f);
}

/** The band of four colours (left to right), as across the foot of the panel. */
inline void drawBand (juce::Graphics& g, juce::Rectangle<float> r, float gap = 0.0f)
{
    const float w = (r.getWidth() - gap * 3.0f) / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (band (i));
        g.fillRect (juce::Rectangle<float> (r.getX() + (w + gap) * (float) i, r.getY(), w, r.getHeight()));
    }
}

/** A legend with a printed bracket under it, reaching across what it names. */
inline void drawLegend (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size = 10.0f,
                        juce::Colour c = legend(), juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (sans (size));
    g.drawText (text, r, j, false);
}

/** A red LED: a dome, lit or dark. */
inline void drawLed (juce::Graphics& g, juce::Point<float> c, float rad, bool on, juce::Colour colour = ledRed())
{
    if (on)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.45f), c.x, c.y, colour.withAlpha (0.0f), c.x + rad * 3.0f, c.y, true));
        g.fillEllipse (c.x - rad * 3.0f, c.y - rad * 3.0f, rad * 6.0f, rad * 6.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (c.x - rad - 1.0f, c.y - rad - 1.0f, rad * 2.0f + 2.0f, rad * 2.0f + 2.0f);
    g.setGradientFill (juce::ColourGradient (on ? colour.brighter (0.6f) : colour.withMultipliedBrightness (0.28f), c.x - rad * 0.3f, c.y - rad * 0.4f,
                                             on ? colour : colour.withMultipliedBrightness (0.16f), c.x + rad, c.y + rad, true));
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.55f : 0.18f));
    g.fillEllipse (c.x - rad * 0.55f, c.y - rad * 0.6f, rad * 0.6f, rad * 0.45f);
}

/** A square key: a plastic cap in `cap`, a shadow under it, pressed down when `down`. */
inline void drawKey (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour cap, bool down, bool over = false)
{
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0.0f, down ? 1.0f : 2.0f), 2.5f);
    auto k = r.translated (0.0f, down ? 1.0f : 0.0f);
    const auto c = over ? cap.brighter (0.08f) : cap;
    g.setGradientFill (juce::ColourGradient (c.brighter (0.10f), k.getX(), k.getY(), c.darker (down ? 0.25f : 0.15f), k.getX(), k.getBottom(), false));
    g.fillRoundedRectangle (k, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : 0.28f));
    g.drawHorizontalLine ((int) k.getY() + 1, k.getX() + 2.0f, k.getRight() - 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (k, 2.5f, 1.0f);
}

/** Seven-segment LED digits (and - : . and spaces) in `area`, right-aligned like a counter,
    each unlit segment faintly there behind the glass. */
inline void drawSevenSegment (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                              juce::Colour colour = ledRed(), juce::Justification j = juce::Justification::centred)
{
    // segments a..g as bits 0..6
    auto bits = [] (juce::juce_wchar ch) -> int
    {
        switch (ch)
        {
            case '0': return 0x3f; case '1': return 0x06; case '2': return 0x5b; case '3': return 0x4f; case '4': return 0x66;
            case '5': return 0x6d; case '6': return 0x7d; case '7': return 0x07; case '8': return 0x7f; case '9': return 0x6f;
            case '-': return 0x40; case 'A': return 0x77; case 'b': return 0x7c; case 'C': return 0x39; case 'd': return 0x5e;
            case 'E': return 0x79; case 'F': return 0x71; case 'P': return 0x73; case 'r': return 0x50; case 'o': return 0x5c;
            default: return 0;
        }
    };
    const float h = area.getHeight(), w = h * 0.52f, t = juce::jmax (1.6f, h * 0.12f), pitch = w + h * 0.16f;
    const float colonW = h * 0.22f, dotW = h * 0.0f;
    float total = 0.0f;
    for (auto ch : text) total += ch == ':' ? colonW : ch == '.' ? dotW : pitch;
    float x = j.testFlags (juce::Justification::right) ? area.getRight() - total
            : j.testFlags (juce::Justification::left)  ? area.getX()
                                                       : area.getCentreX() - total * 0.5f;
    const float y = area.getY();
    const float slant = 0.08f;                       // a little italic, as they were
    auto at = [&] (float px, float py) { return juce::Point<float> (px + (h - (py - y)) * slant, py); };

    auto segment = [&] (int s, float ox, bool on)
    {
        juce::Path p;
        const float hx = ox, half = h * 0.5f, e = t * 0.5f;
        auto horiz = [&] (float yy)
        {
            p.startNewSubPath (at (hx + e, yy)); p.lineTo (at (hx + e + e, yy - e)); p.lineTo (at (hx + w - e - e, yy - e));
            p.lineTo (at (hx + w - e, yy)); p.lineTo (at (hx + w - e - e, yy + e)); p.lineTo (at (hx + e + e, yy + e)); p.closeSubPath();
        };
        auto vert = [&] (float xx, float y0, float y1)
        {
            p.startNewSubPath (at (xx, y0 + e)); p.lineTo (at (xx + e, y0 + e + e)); p.lineTo (at (xx + e, y1 - e - e));
            p.lineTo (at (xx, y1 - e)); p.lineTo (at (xx - e, y1 - e - e)); p.lineTo (at (xx - e, y0 + e + e)); p.closeSubPath();
        };
        switch (s)
        {
            case 0: horiz (y + e); break;
            case 1: vert (hx + w - e, y + e, y + half); break;
            case 2: vert (hx + w - e, y + half, y + h - e); break;
            case 3: horiz (y + h - e); break;
            case 4: vert (hx + e, y + half, y + h - e); break;
            case 5: vert (hx + e, y + e, y + half); break;
            case 6: horiz (y + half); break;
            default: break;
        }
        g.setColour (on ? colour : colour.withAlpha (0.07f));
        g.fillPath (p);
    };

    for (auto ch : text)
    {
        if (ch == ':')
        {
            g.setColour (colour);
            const float d = t * 0.9f;
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (at (x + colonW * 0.4f, y + h * 0.32f)));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (at (x + colonW * 0.4f, y + h * 0.72f)));
            x += colonW;
            continue;
        }
        if (ch == '.')
        {
            g.setColour (colour);
            const float d = t * 0.9f;
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (at (x - h * 0.06f, y + h - d * 0.5f)));
            x += dotW;
            continue;
        }
        const int b = bits (ch);
        for (int s = 0; s < 7; ++s) segment (s, x, (b >> s) & 1);
        x += pitch;
    }
}

/** The dark window an LED readout sits behind. */
inline void drawLedWindow (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colour (0xff0b0706));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (juce::Colour (0xff3a0f0a).withAlpha (0.35f));
    g.fillRoundedRectangle (r.reduced (2.0f), 1.5f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r, 2.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + 2.0f, r.getRight() - 2.0f);
}

} // namespace eighties
