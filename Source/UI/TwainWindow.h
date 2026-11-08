#pragma once

// TWAIN's window: the control room (monitor source, level, dim / mute / mono, speakers A B C,
// talkback) and the players' headphone feeds (a level per cue input, the mix and the click,
// per feed).

#include <juce_gui_basics/juce_gui_basics.h>
#include "MelvilleLookAndFeel.h"
#include "../Core/Hub.h"

namespace melville
{

//==============================================================================
inline std::unique_ptr<juce::XmlElement> twainToXml (const TwainNode& t)
{
    auto x = std::make_unique<juce::XmlElement> ("TWAIN");
    x->setAttribute ("source", t.source.load());
    x->setAttribute ("speakers", t.speakers.load());
    x->setAttribute ("level", (double) t.monitorLevel.load());
    x->setAttribute ("dimLevel", (double) t.dimLevel.load());
    x->setAttribute ("talkLevel", (double) t.talkLevel.load());
    x->setAttribute ("mono", t.mono.load());
    for (int s = 0; s < TwainNode::numSpeakers; ++s) x->setAttribute ("trim" + juce::String (s), (double) t.speakerTrim[s].load());
    for (int f = 0; f < TwainNode::numFeeds; ++f)
    {
        auto& fd = t.feeds[(size_t) f];
        auto* e = x->createNewChildElement ("FEED");
        e->setAttribute ("level", (double) fd.level.load());
        e->setAttribute ("mix", (double) fd.mixLevel.load());
        e->setAttribute ("click", (double) fd.clickLevel.load());
        juce::StringArray cues, pans;
        for (int c = 0; c < TwainNode::numCues; ++c) { cues.add (juce::String (fd.cue[c].load(), 3)); pans.add (juce::String (fd.pan[c].load(), 2)); }
        e->setAttribute ("cues", cues.joinIntoString (" "));
        e->setAttribute ("pans", pans.joinIntoString (" "));
    }
    return x;
}

inline void twainFromXml (TwainNode& t, const juce::XmlElement& x)
{
    t.source = x.getIntAttribute ("source", t.source.load());
    t.speakers = x.getIntAttribute ("speakers", t.speakers.load());
    t.monitorLevel = (float) x.getDoubleAttribute ("level", t.monitorLevel.load());
    t.dimLevel = (float) x.getDoubleAttribute ("dimLevel", t.dimLevel.load());
    t.talkLevel = (float) x.getDoubleAttribute ("talkLevel", t.talkLevel.load());
    t.mono = x.getBoolAttribute ("mono", false);
    for (int s = 0; s < TwainNode::numSpeakers; ++s) t.speakerTrim[s] = (float) x.getDoubleAttribute ("trim" + juce::String (s), 1.0);
    int f = 0;
    for (auto* e : x.getChildWithTagNameIterator ("FEED"))
    {
        if (f >= TwainNode::numFeeds) break;
        auto& fd = t.feeds[(size_t) f++];
        fd.level = (float) e->getDoubleAttribute ("level", 0.8);
        fd.mixLevel = (float) e->getDoubleAttribute ("mix", 0.0);
        fd.clickLevel = (float) e->getDoubleAttribute ("click", 0.5);
        const auto cues = juce::StringArray::fromTokens (e->getStringAttribute ("cues"), " ", {});
        const auto pans = juce::StringArray::fromTokens (e->getStringAttribute ("pans"), " ", {});
        for (int c = 0; c < TwainNode::numCues; ++c)
        {
            fd.cue[c] = c < cues.size() ? (float) cues[c].getDoubleValue() : 0.0f;
            fd.pan[c] = c < pans.size() ? (float) pans[c].getDoubleValue() : 0.0f;
        }
    }
}

//==============================================================================
/** What's cabled into each of TWAIN's cue inputs: "3  Hawthorne track 3" or "3  - nothing cabled". */
inline juce::StringArray twainCueNames (Hub& hub)
{
    auto& pb = hub.getPatchbay();
    juce::StringArray names;
    const auto cords = pb.getAllCords();
    for (int c = 0; c < TwainNode::numCues; ++c)
    {
        const int dest = pb.findDest ("twain", "Cue " + juce::String (c + 1));
        juce::StringArray from;
        for (auto& [d, cord] : cords) if (d == dest) from.add (pb.sourceName (cord.source));
        names.add (juce::String (c + 1) + "  " + (from.isEmpty() ? juce::String ("- nothing cabled") : from.joinIntoString (" + ")));
    }
    return names;
}

//==============================================================================
/** TWAIN's hardware: a monitor controller's faceplate. Machined aluminium knob for the
    control room, small black knobs for the cue mixes, illuminated push buttons, LED ladders. */
class TwainLookAndFeel : public MelvilleLookAndFeel
{
public:
    static juce::Colour orange()  { return Colours::forPart ("twain"); }
    static juce::Colour silk()    { return juce::Colour (0xffe9e2d2); }            // the panel's printing
    static juce::Colour plate()   { return juce::Colour (0xff26272a); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        const auto c = r.getCentre();
        const float angle = start + pos * (end - start);
        const bool big = size > 90.0f;
        const bool aluminium = big || s.getProperties().contains ("aluminium");

        // the scale printed round it
        const float scaleR = size * 0.5f - 1.0f;
        const int ticks = big ? 21 : (aluminium ? 11 : 0);      // (the cue knobs sit close: no scale)
        g.setColour (silk().withAlpha (0.55f));
        for (int i = 0; i < ticks; ++i)
        {
            const float a = start + (end - start) * (float) i / (float) (ticks - 1);
            const bool major = i % (big ? 5 : 5) == 0;
            const float r0 = scaleR - (major ? (big ? 9.0f : 4.0f) : (big ? 5.0f : 2.5f));
            g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + scaleR * std::sin (a), c.y - scaleR * std::cos (a), major ? 1.5f : 1.0f);
        }

        const float knobR = scaleR - (big ? 14.0f : aluminium ? 5.0f : 1.0f);
        // shadow
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (c.translated (0.0f, knobR * 0.08f + 1.0f)));
        if (aluminium)
        {
            // machined aluminium: a knurled skirt, a brushed cap
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d8d6), c.x, c.y - knobR, juce::Colour (0xff7b7c7e), c.x, c.y + knobR, false));
            g.fillEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (c));
            g.setColour (juce::Colour (0xff55565a));
            const int knurls = big ? 72 : 36;
            for (int i = 0; i < knurls; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / (float) knurls + angle;
                g.drawLine (c.x + knobR * 0.86f * std::sin (a), c.y - knobR * 0.86f * std::cos (a),
                            c.x + knobR * std::sin (a), c.y - knobR * std::cos (a), 1.0f);
            }
            const float capR = knobR * 0.82f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeeec), c.x - capR, c.y - capR, juce::Colour (0xff9a9b9d), c.x + capR, c.y + capR, false));
            g.fillEllipse (juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c));
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            for (float k = 0.25f; k < 1.0f; k += 0.18f)
                g.drawEllipse (juce::Rectangle<float> (capR * 2.0f * k, capR * 2.0f * k).withCentre (c), 0.6f);
            // the pointer: an engraved line, filled black
            juce::Path p;
            p.addRoundedRectangle (-1.6f, -capR + 3.0f, 3.2f, capR * 0.5f, 1.2f);
            g.setColour (juce::Colour (0xff141414));
            g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
        }
        else
        {
            // a small black knob with a white line
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3e41), c.x, c.y - knobR, juce::Colour (0xff111213), c.x, c.y + knobR, false));
            g.fillEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (c));
            g.setColour (juce::Colour (0xff4a4b4f));
            g.drawEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (c), 1.0f);
            juce::Path p;
            p.addRoundedRectangle (-1.1f, -knobR + 1.5f, 2.2f, knobR * 0.7f, 1.0f);
            g.setColour (pos > 0.001f ? juce::Colour (0xfff4efe4) : juce::Colour (0xff8a8a88));
            g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        // an illuminated push button: a dark cap in a bezel; on, its lens glows
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const bool on = b.getToggleState() || (b.getProperties().contains ("momentary") && down);
        const auto lamp = b.getProperties().contains ("lamp") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["lamp"]) : orange();
        g.setColour (juce::Colour (0xff0e0f10));
        g.fillRoundedRectangle (r, 4.0f);                                          // the bezel
        auto cap = r.reduced (2.5f).translated (0.0f, down ? 1.0f : 0.0f);
        g.setGradientFill (juce::ColourGradient (on ? lamp.withMultipliedBrightness (0.55f) : juce::Colour (0xff3a3b3e), cap.getX(), cap.getY(),
                                                 on ? lamp.withMultipliedBrightness (0.32f) : juce::Colour (0xff202124), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 3.0f);
        // the lens across the top of the cap
        auto lens = cap.reduced (cap.getWidth() * 0.18f, 0.0f).removeFromTop (juce::jmax (3.0f, cap.getHeight() * 0.16f)).translated (0.0f, cap.getHeight() * 0.12f);
        if (on)
        {
            g.setColour (lamp.withAlpha (0.35f));
            g.fillRoundedRectangle (lens.expanded (3.0f, 2.0f), 3.0f);              // the glow
            g.setColour (lamp.brighter (0.4f));
        }
        else g.setColour (juce::Colour (0xff141516));
        g.fillRoundedRectangle (lens, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.12f : 0.06f));
        g.drawRoundedRectangle (cap, 3.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        auto r = b.getLocalBounds().reduced (4).withTrimmedTop (juce::jmax (6, b.getHeight() / 4)).translated (0, down ? 1 : 0);
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (10.0f, 14.0f, (float) b.getHeight() * 0.36f), juce::Font::bold)));
        g.setColour (b.getToggleState() ? juce::Colours::white : silk().withAlpha (0.85f));
        g.drawFittedText (b.getButtonText(), r, juce::Justification::centred, 1);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal) return MelvilleLookAndFeel::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        // a small horizontal fader: a slot and a cap
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto slot = r.withSizeKeepingCentre (r.getWidth() - 10.0f, 4.0f);
        g.setColour (juce::Colour (0xff0b0c0d));
        g.fillRoundedRectangle (slot, 2.0f);
        const float cx = juce::jlimit (slot.getX(), slot.getRight(), pos);
        auto cap = juce::Rectangle<float> (12.0f, juce::jmin (r.getHeight(), 22.0f)).withCentre ({ cx, r.getCentreY() });
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd5d5d3), cap.getX(), cap.getY(), juce::Colour (0xff7a7b7d), cap.getX(), cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (0xff222222));
        g.drawLine (cx, cap.getY() + 3.0f, cx, cap.getBottom() - 3.0f, 1.5f);
    }
};

//==============================================================================
class TwainPanel : public juce::Component, private juce::Timer
{
public:
    explicit TwainPanel (Hub& h) : hub (h), twain (h.getTwain())
    {
        setLookAndFeel (&lnf);
        const auto accent = Colours::forPart ("twain");

        // --- monitor source
        for (int s = 0; s < TwainNode::numSources; ++s)
        {
            const juce::String name = s == TwainNode::mix ? "MIX" : s == TwainNode::twoTrack ? "2-TRACK" : s == TwainNode::ext1 ? "EXT 1"
                                    : s == TwainNode::ext2 ? "EXT 2" : "PH " + juce::String (s - TwainNode::feed1 + 1);
            auto* b = sources.add (new juce::TextButton (name));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1);
            b->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
            b->onClick = [this, s] { twain.source = s; };
            b->setTooltip (s == TwainNode::mix ? "Listen to the desk: the mix, live (TWAIN's MIX IN)"
                         : s == TwainNode::twoTrack ? "Listen to the tape: Hawthorne's master tape (TWAIN's 2-TRACK IN)"
                         : s == TwainNode::ext1 ? "Listen to TWAIN's EXT 1 IN" : s == TwainNode::ext2 ? "Listen to TWAIN's EXT 2 IN"
                         : "Listen to what headphone feed " + juce::String (s - TwainNode::feed1 + 1) + " is hearing");
            addAndMakeVisible (b);
        }
        // --- speakers
        for (int s = 0; s < TwainNode::numSpeakers; ++s)
        {
            auto* b = speakerButtons.add (new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + s))));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (2);
            b->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
            b->onClick = [this, s] { twain.speakers = s; };
            b->setTooltip ("Speaker set " + b->getButtonText() + " (TWAIN's SPEAKERS " + b->getButtonText() + " jacks). Right-click: its trim");
            b->addMouseListener (this, false);
            addAndMakeVisible (b);
        }
        // --- level, dim, mute, mono, talk
        level.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        level.setRange (-60.0, 6.0, 0.5);
        level.setSkewFactorFromMidPoint (-12.0);
        level.setTextValueSuffix (" dB");
        level.setDoubleClickReturnValue (true, 0.0);
        level.setColour (juce::Slider::rotarySliderFillColourId, accent);
        level.setColour (juce::Slider::thumbColourId, accent.brighter (0.3f));
        level.onValueChange = [this] { twain.monitorLevel = level.getValue() <= -59.9 ? 0.0f : juce::Decibels::decibelsToGain ((float) level.getValue()); };
        level.setTooltip ("Control room level");
        addAndMakeVisible (level);
        for (auto* b : { &dim, &mute, &mono })
        {
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
            addAndMakeVisible (b);
        }
        mute.setColour (juce::TextButton::buttonOnColourId, Colours::warn);
        dim.onClick = [this] { twain.dim = dim.getToggleState(); };
        mute.onClick = [this] { twain.mute = mute.getToggleState(); };
        mono.onClick = [this] { twain.mono = mono.getToggleState(); };
        dim.setTooltip ("Dim the speakers (right-click: by how much)");
        dim.addMouseListener (this, false);
        mute.setTooltip ("Mute the speakers");
        mono.setTooltip ("Listen in mono");
        talk.getProperties().set ("momentary", true);
        talk.getProperties().set ("lamp", (juce::int64) juce::Colour (0xffe0402e).getARGB());
        mute.getProperties().set ("lamp", (juce::int64) juce::Colour (0xffe0402e).getARGB());
        talk.setTooltip ("Hold to talk to the players: the TALKBACK jack into every headphone feed, and the speakers dim");
        talk.onStateChange = [this] { const bool down = talk.isDown(); if (down != twain.talk.load()) twain.talk = down; };
        addAndMakeVisible (talk);
        knob (talkLevel, "Talkback level", [this] (double v) { twain.talkLevel = (float) v; }, 0.0, 2.0, 1.0);

        // --- headphone feeds: a knob per cue input, the mix and the click; the feed's level
        for (int f = 0; f < TwainNode::numFeeds; ++f)
        {
            auto& fd = twain.feeds[(size_t) f];
            for (int c = 0; c < TwainNode::numCues; ++c)
            {
                auto* k = cells.add (new juce::Slider());
                knob (*k, {}, [&fd, c] (double v) { fd.cue[c] = (float) v; }, 0.0, 1.0, 0.0);
                k->getProperties().set ("feed", f);
                k->getProperties().set ("cue", c);
                k->addMouseListener (this, false);
            }
            auto* m = mixKnobs.add (new juce::Slider());
            knob (*m, "The mix in headphone feed " + juce::String (f + 1), [&fd] (double v) { fd.mixLevel = (float) v; }, 0.0, 1.0, 0.0);
            auto* k = clickKnobs.add (new juce::Slider());
            knob (*k, "The click in headphone feed " + juce::String (f + 1), [&fd] (double v) { fd.clickLevel = (float) v; }, 0.0, 1.0, 0.5);
            auto* lv = feedLevels.add (new juce::Slider());
            lv->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            lv->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            lv->setRange (0.0, 1.5, 0.01);
            lv->setDoubleClickReturnValue (true, 0.8);
            lv->getProperties().set ("aluminium", true);
            lv->onValueChange = [this, f, lv] { if (! updating) twain.feeds[(size_t) f].level = (float) lv->getValue(); };
            lv->setTooltip ("Headphone feed " + juce::String (f + 1) + "'s level");
            addAndMakeVisible (lv);
        }

        refresh();
        setSize (1500, 740);
        startTimerHz (20);
    }

    ~TwainPanel() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        const auto orange = TwainLookAndFeel::orange(), silk = TwainLookAndFeel::silk();
        g.fillAll (juce::Colour (0xff121314));
        for (auto& [area, title] : { std::pair (roomArea, "CONTROL ROOM"), std::pair (feedsArea, "CUE  /  HEADPHONES") })
        {
            // a brushed steel faceplate, with its screws
            auto a = area.toFloat();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff303135), a.getX(), a.getY(), juce::Colour (0xff1e1f22), a.getX(), a.getBottom(), false));
            g.fillRoundedRectangle (a, 4.0f);
            juce::Random brush (7);
            for (float y = a.getY() + 2.0f; y < a.getBottom() - 2.0f; y += 2.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.012f + 0.02f * brush.nextFloat()));
                g.drawHorizontalLine ((int) y, a.getX() + 2.0f, a.getRight() - 2.0f);
            }
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (a.reduced (0.5f), 4.0f, 1.0f);
            for (auto p : { a.getTopLeft() + juce::Point<float> (10, 10), a.getTopRight() + juce::Point<float> (-10, 10),
                            a.getBottomLeft() + juce::Point<float> (10, -10), a.getBottomRight() + juce::Point<float> (-10, -10) })
            {
                g.setColour (juce::Colour (0xff111111));
                g.fillEllipse (juce::Rectangle<float> (9, 9).withCentre (p));
                g.setColour (juce::Colour (0xff6d6e70));
                g.drawLine (p.x - 3.0f, p.y - 1.0f, p.x + 3.0f, p.y + 1.0f, 1.5f);
            }
            // the printed name, with an orange rule
            g.setColour (orange);
            g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
            auto t = area.reduced (24, 12).removeFromTop (20);
            g.drawText (title, t, juce::Justification::centredLeft);
            g.fillRect (t.getX(), t.getBottom() + 2, t.getWidth(), 2);
        }
        // the maker's plate
        g.setColour (silk.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("TWAIN  \xc2\xb7  MONITOR CONTROLLER")), roomArea.reduced (24, 14).removeFromBottom (14), juce::Justification::centredLeft);

        // silkscreen over each group
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        for (auto& [r, text] : { std::pair (sourceCaption, "SOURCE"), std::pair (speakerCaption, "SPEAKERS"), std::pair (talkCaption, "TALKBACK") })
        {
            g.setColour (silk.withAlpha (0.8f));
            g.drawText (text, r, juce::Justification::centredLeft);
            const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 8;
            g.setColour (silk.withAlpha (0.25f));
            g.drawHorizontalLine (r.getCentreY(), (float) (r.getX() + tw), (float) r.getRight());
        }

        // the level knob's scale numbers
        {
            const auto c = level.getBounds().toFloat().getCentre();
            const float rad = (float) juce::jmin (level.getWidth(), level.getHeight()) * 0.5f + 10.0f;
            const auto rp = level.getRotaryParameters();
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.setColour (silk.withAlpha (0.7f));
            for (double db : { -60.0, -40.0, -30.0, -20.0, -12.0, -6.0, 0.0, 6.0 })
            {
                const float a = rp.startAngleRadians + (float) level.valueToProportionOfLength (db) * (rp.endAngleRadians - rp.startAngleRadians);
                const auto at = juce::Point<float> (c.x + rad * std::sin (a), c.y - rad * std::cos (a));
                g.drawText (db <= -59.0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")) : juce::String ((int) db),
                            juce::Rectangle<float> (26, 12).withCentre (at), juce::Justification::centred);
            }
            g.setColour (silk.withAlpha (0.85f));
            g.drawText ("LEVEL", juce::Rectangle<float> (80, 14).withCentre ({ c.x, c.y + rad + 2.0f }), juce::Justification::centred);
        }

        // the monitor's LED ladder
        drawMeter (g, monitorMeter, shownMonitor);

        // the cue grid: row names, column heads, a meter and a level per feed
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        for (int r = 0; r < (int) rowAreas.size(); ++r)
        {
            if (r == TwainNode::numCues)
            {
                g.setColour (silk.withAlpha (0.2f));
                g.drawHorizontalLine (rowAreas[(size_t) r].getY(), (float) rowAreas[(size_t) r].getX(), (float) feedsArea.getRight() - 24.0f);
            }
            g.setColour (r < TwainNode::numCues && rowNames[r].contains ("nothing") ? silk.withAlpha (0.35f) : silk.withAlpha (0.9f));
            g.drawFittedText (rowNames[r], rowAreas[(size_t) r].withWidth (rowLabelW - 6).reduced (6, 0), juce::Justification::centredLeft, 1);
        }
        // where the players set their own mixes
        if (phonesUrl)
        {
            const auto url = phonesUrl();
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.setColour (silk.withAlpha (0.75f));
            g.drawText (url.isNotEmpty() ? "ON A PHONE:  " + url : juce::String ("(the phones page isn't running)"),
                        feedsArea.reduced (24, 12).removeFromTop (20), juce::Justification::centredRight);
        }
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        for (int f = 0; f < TwainNode::numFeeds; ++f)
        {
            g.setColour (orange);
            g.drawText ("PHONES " + juce::String (f + 1), colHeads[(size_t) f], juce::Justification::centred);
            drawMeter (g, feedMeters[(size_t) f], shownFeed[(size_t) f]);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        roomArea = r.removeFromLeft (300);
        r.removeFromLeft (10);
        feedsArea = r;

        {
            auto a = roomArea.reduced (24, 12).withTrimmedTop (34);
            sourceCaption = a.removeFromTop (16);
            // MIX 2-TRACK EXT 1 EXT 2, then the headphone feeds four to a row
            juce::Rectangle<int> row;
            for (int s = 0; s < TwainNode::numSources; ++s)
            {
                if (s % 4 == 0) { if (s > 0) a.removeFromTop (s == TwainNode::feed1 ? 8 : 4); row = a.removeFromTop (s < TwainNode::feed1 ? 30 : 26); }
                sources[s]->setBounds (row.removeFromLeft (row.getWidth() / (4 - s % 4)).reduced (2, 0));
            }
            a.removeFromTop (14);
            auto lv = a.removeFromTop (190);
            monitorMeter = lv.removeFromRight (14).reduced (0, 16);
            lv.removeFromRight (6);
            level.setBounds (lv.withSizeKeepingCentre (150, 150).translated (0, -10));
            a.removeFromTop (6);
            row = a.removeFromTop (32);
            const int w3 = row.getWidth() / 3;
            for (auto* b : { &dim, &mute, &mono }) b->setBounds (row.removeFromLeft (w3).reduced (2, 0));
            a.removeFromTop (14);
            speakerCaption = a.removeFromTop (16);
            row = a.removeFromTop (32);
            for (auto* b : speakerButtons) b->setBounds (row.removeFromLeft (w3).reduced (2, 0));
            a.removeFromTop (14);
            talkCaption = a.removeFromTop (16);
            row = a.removeFromTop (40);
            talk.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 2));
            row.removeFromLeft (8);
            talkLevel.setBounds (row.withSizeKeepingCentre (row.getWidth(), 14));
        }

        {
            auto a = feedsArea.reduced (24, 12).withTrimmedTop (34);
            auto heads = a.removeFromTop (22);
            auto levels = a.removeFromBottom (110);
            heads.removeFromLeft (rowLabelW);
            levels.removeFromLeft (rowLabelW);
            const int colW = heads.getWidth() / TwainNode::numFeeds;
            colHeads.clear();
            feedMeters.clear();
            for (int f = 0; f < TwainNode::numFeeds; ++f)
            {
                colHeads.push_back (heads.removeFromLeft (colW));
                auto lv = levels.removeFromLeft (colW).reduced (juce::jmax (4, colW / 2 - 34), 6);
                feedMeters.push_back (lv.removeFromRight (10));
                feedLevels[f]->setBounds (lv.withTrimmedRight (6));
            }
            const int rows = TwainNode::numCues + 2;
            const int rowH = juce::jmax (18, a.getHeight() / rows);
            rowAreas.clear();
            for (int row = 0; row < rows; ++row)
            {
                auto rr = a.removeFromTop (rowH);
                rowAreas.push_back (rr);
                rr.removeFromLeft (rowLabelW);
                for (int f = 0; f < TwainNode::numFeeds; ++f)
                {
                    auto cell = rr.removeFromLeft (colW).withSizeKeepingCentre (rowH + 2, rowH + 2);
                    juce::Slider* k = row < TwainNode::numCues ? cells[f * TwainNode::numCues + row]
                                    : row == TwainNode::numCues ? mixKnobs[f] : clickKnobs[f];
                    k->setBounds (cell);
                }
            }
        }
    }

    std::function<juce::String()> phonesUrl;      // the players' web page (shown on the panel)

    void refresh()
    {
        const juce::ScopedValueSetter<bool> u (updating, true);
        for (int s = 0; s < sources.size(); ++s) sources[s]->setToggleState (twain.source.load() == s, juce::dontSendNotification);
        for (int s = 0; s < speakerButtons.size(); ++s) speakerButtons[s]->setToggleState (twain.speakers.load() == s, juce::dontSendNotification);
        dim.setToggleState (twain.dim.load(), juce::dontSendNotification);
        mute.setToggleState (twain.mute.load(), juce::dontSendNotification);
        mono.setToggleState (twain.mono.load(), juce::dontSendNotification);
        level.setValue (twain.monitorLevel.load() <= 0.0f ? -60.0 : juce::Decibels::gainToDecibels (twain.monitorLevel.load()), juce::dontSendNotification);
        talkLevel.setValue (twain.talkLevel.load(), juce::dontSendNotification);
        for (int f = 0; f < TwainNode::numFeeds; ++f)
        {
            auto& fd = twain.feeds[(size_t) f];
            for (int c = 0; c < TwainNode::numCues; ++c) cells[f * TwainNode::numCues + c]->setValue (fd.cue[c].load(), juce::dontSendNotification);
            mixKnobs[f]->setValue (fd.mixLevel.load(), juce::dontSendNotification);
            clickKnobs[f]->setValue (fd.clickLevel.load(), juce::dontSendNotification);
            feedLevels[f]->setValue (fd.level.load(), juce::dontSendNotification);
        }
        updateRowNames();
    }

private:
    // a level, as a bar that fills: drag across it, double-click for its default
    void knob (juce::Slider& k, const juce::String& tip, std::function<void (double)> set, double lo, double hi, double dflt)
    {
        k.setSliderStyle (&k == &talkLevel ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRange (lo, hi, 0.01);
        k.setDoubleClickReturnValue (true, dflt);
        k.onValueChange = [this, &k, set] { if (! updating) set (k.getValue()); };
        if (tip.isNotEmpty()) k.setTooltip (tip);
        addAndMakeVisible (k);
    }

    // an LED ladder: green, then amber from -12, red from -3
    static void drawMeter (juce::Graphics& g, juce::Rectangle<int> r, float v)
    {
        if (r.isEmpty()) return;
        g.setColour (juce::Colour (0xff0a0b0c));
        g.fillRoundedRectangle (r.toFloat(), 2.0f);
        const float db = juce::Decibels::gainToDecibels (v, -60.0f);
        const int leds = juce::jmax (6, r.getHeight() / 7);
        const float ledH = (float) (r.getHeight() - 4) / (float) leds;
        for (int i = 0; i < leds; ++i)
        {
            const float at = -60.0f + 63.0f * (float) (i + 1) / (float) leds;     // this LED's level
            const auto col = at > -3.0f ? juce::Colour (0xffe8442e) : at > -12.0f ? juce::Colour (0xffe8b02e) : juce::Colour (0xff5ccf5a);
            auto led = juce::Rectangle<float> ((float) r.getX() + 2.0f, (float) r.getBottom() - 2.0f - ledH * (float) (i + 1),
                                               (float) r.getWidth() - 4.0f, ledH - 1.5f);
            g.setColour (db >= at - 63.0f / (float) leds ? col : col.withAlpha (0.12f));
            g.fillRoundedRectangle (led, 1.0f);
        }
    }

    // a cue row is named after what's cabled into it
    void updateRowNames()
    {
        rowNames = twainCueNames (hub);
        rowNames.add ("MIX");
        rowNames.add ("CLICK");
        for (int f = 0; f < TwainNode::numFeeds; ++f)
            for (int c = 0; c < TwainNode::numCues; ++c)
                cells[f * TwainNode::numCues + c]->setTooltip ("Cue " + rowNames[c] + " in headphone feed " + juce::String (f + 1)
                                                                + ". Right-click: pan");
    }

    void timerCallback() override
    {
        // the meters fall back like a PPM
        auto fall = [] (float& shown, std::atomic<float>& peak) { shown = juce::jmax (peak.exchange (0.0f), shown * 0.8f); };
        fall (shownMonitor, twain.monitorPeak);
        for (int f = 0; f < TwainNode::numFeeds; ++f) fall (shownFeed[(size_t) f], twain.feedPeak[(size_t) f]);
        repaint (monitorMeter.expanded (2));
        for (auto& m : feedMeters) repaint (m.expanded (2));
        if (++ticks % 20 == 0) { updateRowNames(); repaint (feedsArea); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        auto* c = e.eventComponent;
        if (auto* k = dynamic_cast<juce::Slider*> (c); k != nullptr && k->getProperties().contains ("cue"))
        {
            // a cue's pan in this feed
            const int f = k->getProperties()["feed"], cue = k->getProperties()["cue"];
            auto& pan = twain.feeds[(size_t) f].pan[cue];
            juce::PopupMenu m;
            m.addSectionHeader ("PAN");
            const std::pair<const char*, float> stops[] = { { "Left", -1.0f }, { "Half left", -0.5f }, { "Centre", 0.0f }, { "Half right", 0.5f }, { "Right", 1.0f } };
            for (auto& [name, v] : stops)
                m.addItem (name, true, std::abs (pan.load() - v) < 0.01f, [&pan, v = v] { pan = v; });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (k));
        }
        else if (c == &dim)
        {
            juce::PopupMenu m;
            for (int db : { -10, -15, -20, -30, -40 })
                m.addItem (juce::String (db) + " dB", true, std::abs (juce::Decibels::gainToDecibels (twain.dimLevel.load()) - (float) db) < 0.5f,
                           [this, db] { twain.dimLevel = juce::Decibels::decibelsToGain ((float) db); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dim));
        }
        else
            for (int s = 0; s < speakerButtons.size(); ++s)
                if (c == speakerButtons[s])
                {
                    juce::PopupMenu m;
                    m.addSectionHeader ("SPEAKERS " + speakerButtons[s]->getButtonText() + " TRIM");
                    for (int db : { 0, -1, -2, -3, -4, -6, -8, -10, -12 })
                        m.addItem (juce::String (db) + " dB", true, std::abs (juce::Decibels::gainToDecibels (twain.speakerTrim[s].load()) - (float) db) < 0.25f,
                                   [this, s, db] { twain.speakerTrim[s] = juce::Decibels::decibelsToGain ((float) db); });
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (speakerButtons[s]));
                }
    }

    TwainLookAndFeel lnf;
    Hub& hub;
    TwainNode& twain;
    bool updating = false;
    int ticks = 0;
    static constexpr int rowLabelW = 230;

    juce::OwnedArray<juce::TextButton> sources, speakerButtons;
    juce::Slider level, talkLevel;
    juce::TextButton dim { "DIM" }, mute { "MUTE" }, mono { "MONO" }, talk { "TALK" };
    juce::OwnedArray<juce::Slider> cells, mixKnobs, clickKnobs, feedLevels;

    juce::Rectangle<int> roomArea, feedsArea, sourceCaption, speakerCaption, talkCaption, monitorMeter;
    std::vector<juce::Rectangle<int>> rowAreas, colHeads, feedMeters;
    juce::StringArray rowNames;
    float shownMonitor = 0.0f;
    std::array<float, TwainNode::numFeeds> shownFeed {};
};

} // namespace melville
