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
class TwainPanel : public juce::Component, private juce::Timer
{
public:
    explicit TwainPanel (Hub& h) : hub (h), twain (h.getTwain())
    {
        const auto accent = Colours::forPart ("twain");

        // --- monitor source
        const char* sourceNames[] = { "MIX", "2-TRACK", "EXT", "PH 1", "PH 2", "PH 3", "PH 4" };
        for (int s = 0; s < TwainNode::numSources; ++s)
        {
            auto* b = sources.add (new juce::TextButton (sourceNames[s]));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1);
            b->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
            b->onClick = [this, s] { twain.source = s; };
            b->setTooltip (s == 0 ? "Listen to the mix (TWAIN's MIX IN)" : s == 1 ? "Listen to the 2-track: Hawthorne's master (TWAIN's 2-TRACK IN)"
                         : s == 2 ? "Listen to TWAIN's EXT IN" : "Listen to what headphone feed " + juce::String (s - 2) + " is hearing");
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
        level.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 20);
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
        talk.setColour (juce::TextButton::buttonColourId, Colours::panelHi);
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
            lv->setSliderStyle (juce::Slider::LinearVertical);
            lv->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            lv->setRange (0.0, 1.5, 0.01);
            lv->setDoubleClickReturnValue (true, 0.8);
            lv->setColour (juce::Slider::thumbColourId, accent);
            lv->setColour (juce::Slider::trackColourId, accent.darker (0.4f));
            lv->onValueChange = [this, f, lv] { if (! updating) twain.feeds[(size_t) f].level = (float) lv->getValue(); };
            lv->setTooltip ("Headphone feed " + juce::String (f + 1) + "'s level");
            addAndMakeVisible (lv);
        }

        refresh();
        setSize (1040, 640);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        const auto accent = Colours::forPart ("twain");
        g.fillAll (Colours::bg);
        for (auto& [area, title] : { std::pair (roomArea, "CONTROL ROOM"), std::pair (feedsArea, "HEADPHONES") })
        {
            g.setColour (Colours::panel);
            g.fillRoundedRectangle (area.toFloat(), 6.0f);
            g.setColour (accent);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (title, area.reduced (14, 8).removeFromTop (18), juce::Justification::centredLeft);
        }
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.setColour (Colours::textDim);
        g.drawText ("LISTEN TO", sourceCaption, juce::Justification::centredLeft);
        g.drawText ("SPEAKERS", speakerCaption, juce::Justification::centredLeft);
        g.drawText ("TALKBACK", talkCaption, juce::Justification::centredLeft);

        // the monitor's meter
        drawMeter (g, monitorMeter, shownMonitor, accent);

        // the feeds' grid: row names, column heads, meters
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        for (int r = 0; r < (int) rowAreas.size(); ++r)
        {
            g.setColour (r % 2 == 0 ? Colours::panelHi.withAlpha (0.35f) : juce::Colours::transparentBlack);
            g.fillRect (rowAreas[(size_t) r].withRight (feedsArea.getRight() - 14));
            g.setColour (r < TwainNode::numCues && rowNames[r].startsWith ("-") ? Colours::textDim.withAlpha (0.6f) : Colours::text);
            g.drawFittedText (rowNames[r], rowAreas[(size_t) r].withWidth (rowLabelW - 6).reduced (6, 0), juce::Justification::centredLeft, 1);
        }
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        for (int f = 0; f < TwainNode::numFeeds; ++f)
        {
            g.setColour (accent);
            g.drawText ("PHONES " + juce::String (f + 1), colHeads[(size_t) f], juce::Justification::centred);
            drawMeter (g, feedMeters[(size_t) f], shownFeed[(size_t) f], accent);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        roomArea = r.removeFromLeft (300);
        r.removeFromLeft (10);
        feedsArea = r;

        {
            auto a = roomArea.reduced (14, 8).withTrimmedTop (28);
            sourceCaption = a.removeFromTop (16);
            auto row = a.removeFromTop (30);
            const int w = row.getWidth() / 3;
            for (int s = 0; s < 3; ++s) sources[s]->setBounds (row.removeFromLeft (w).reduced (2, 0));
            a.removeFromTop (4);
            row = a.removeFromTop (30);
            const int w4 = row.getWidth() / 4;
            for (int s = 3; s < TwainNode::numSources; ++s) sources[s]->setBounds (row.removeFromLeft (w4).reduced (2, 0));
            a.removeFromTop (14);
            auto lv = a.removeFromTop (170);
            monitorMeter = lv.removeFromRight (14).reduced (0, 10);
            lv.removeFromRight (6);
            level.setBounds (lv.withSizeKeepingCentre (160, 170));
            a.removeFromTop (10);
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
            auto a = feedsArea.reduced (14, 8).withTrimmedTop (28);
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
                auto lv = levels.removeFromLeft (colW).reduced (colW / 2 - 24, 6);
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
                    auto cell = rr.removeFromLeft (colW).reduced (14, juce::jmax (3, rowH / 4));
                    juce::Slider* k = row < TwainNode::numCues ? cells[f * TwainNode::numCues + row]
                                    : row == TwainNode::numCues ? mixKnobs[f] : clickKnobs[f];
                    k->setBounds (cell);
                }
            }
        }
    }

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
        k.setSliderStyle (juce::Slider::LinearBar);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setColour (juce::Slider::trackColourId, Colours::forPart ("twain").withAlpha (0.85f));
        k.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff141516));
        k.setColour (juce::Slider::textBoxOutlineColourId, Colours::line);
        k.setVelocityBasedMode (false);
        k.setRange (lo, hi, 0.01);
        k.setDoubleClickReturnValue (true, dflt);
        k.onValueChange = [this, &k, set] { if (! updating) set (k.getValue()); };
        if (tip.isNotEmpty()) k.setTooltip (tip);
        addAndMakeVisible (k);
    }

    static void drawMeter (juce::Graphics& g, juce::Rectangle<int> r, float v, juce::Colour c)
    {
        if (r.isEmpty()) return;
        g.setColour (juce::Colour (0xff101112));
        g.fillRect (r);
        const float db = juce::Decibels::gainToDecibels (v, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto lit = r.toFloat().withTrimmedTop ((float) r.getHeight() * (1.0f - frac));
        g.setColour (db > -1.0f ? Colours::warn : c);
        g.fillRect (lit);
    }

    // a cue row is named after what's cabled into it
    void updateRowNames()
    {
        auto& pb = hub.getPatchbay();
        rowNames.clearQuick();
        for (int c = 0; c < TwainNode::numCues; ++c)
        {
            const int dest = pb.findDest ("twain", "Cue " + juce::String (c + 1));
            juce::StringArray from;
            for (auto& [d, cord] : pb.getAllCords()) if (d == dest) from.add (pb.sourceName (cord.source));
            rowNames.add (juce::String (c + 1) + "  " + (from.isEmpty() ? juce::String ("- nothing cabled") : from.joinIntoString (" + ")));
        }
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
