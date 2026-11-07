#include "PatchbayView.h"
#include "MelvilleLookAndFeel.h"
#include "../Core/NetworkNode.h"
#include <map>

namespace melville
{

namespace
{
    constexpr float jackPitch = 22.0f, jackRadius = 6.5f, sectionGap = 18.0f, titleH = 15.0f, labelH = 12.0f;
    constexpr float panelPad = 12.0f, nameColumn = 132.0f, panelGap = 26.0f, margin = 16.0f;
    constexpr float rowH = titleH + jackPitch + labelH + 6.0f;

    juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

    juce::StringArray numbered (const juce::String& prefix, int n, int first = 1)
    {
        juce::StringArray s;
        for (int i = 0; i < n; ++i) s.add (prefix + juce::String (first + i));
        return s;
    }

    juce::StringArray numbers (int n)
    {
        juce::StringArray s;
        for (int i = 1; i <= n; ++i) s.add (juce::String (i));
        return s;
    }
}

//==============================================================================
PatchCanvas::PatchCanvas (Hub& h) : hub (h)
{
    hub.getPatchbay().onChange = [this] { repaint(); };
    rebuild();
    startTimerHz (4);
}

PatchCanvas::~PatchCanvas()
{
    hub.getPatchbay().onChange = nullptr;
}

void PatchCanvas::rebuild()
{
    auto& pb = hub.getPatchbay();
    const int tracks = hub.getVisibleTracks();
    int ins = 8, outs = 8;
    if (auto* dev = hub.deviceManager.getCurrentAudioDevice())
    {
        ins = juce::jlimit (2, 64, dev->getActiveInputChannels().countNumberOfSetBits());
        outs = juce::jlimit (2, 64, dev->getActiveOutputChannels().countNumberOfSetBits());
    }
    lastTracks = tracks; lastIns = ins; lastOuts = outs;
    lastNetwork = networkKey();

    panels.clear();

    auto panel = [this] (const juce::String& id, const juce::String& title) -> Panel&
    {
        Panel p;
        p.id = id;
        p.title = title;
        p.colour = Colours::forPart (id);
        panels.push_back (p);
        return panels.back();
    };

    // a section of jacks: channels of one port, with the labels under them
    auto section = [&pb] (Panel& p, const juce::String& title, bool output, const juce::String& portId,
                          const juce::StringArray& channels, const juce::StringArray& labels)
    {
        Section s;
        s.title = title;
        s.output = output;
        for (int i = 0; i < channels.size(); ++i)
        {
            const int flat = output ? pb.findSource (portId, channels[i]) : pb.findDest (portId, channels[i]);
            if (flat < 0) continue;
            Jack j;
            j.flat = flat;
            j.label = labels[i];
            s.jacks.push_back (j);
        }
        if (! s.jacks.empty()) p.sections.push_back (std::move (s));
    };

    const juce::StringArray lr { "L", "R" };

    {   auto& p = panel ("interface", "INTERFACE");
        section (p, "INPUTS", true, "interface", numbered ("In ", ins), numbers (ins));
        section (p, "OUTPUTS", false, "interface", numbered ("Out ", outs), numbers (outs)); }

    {   // Hawthorne Link: a block of jacks per connected sender
        auto& p = panel ("network", "NETWORK");
        auto& rx = hub.getNetwork().getReceiver();
        for (int slot = 0; slot < NetworkNode::slots; ++slot)
        {
            const auto st = rx.getStatus (slot);
            if (st.state == hawthorne::NetReceiver::State::off || st.channels <= 0) continue;
            juce::StringArray chans;
            for (int c = 0; c < st.channels; ++c) chans.add (NetworkNode::channelName (slot, c));
            section (p, juce::String (slot + 1) + " " + st.name.toUpperCase(), true, "network", chans, numbers (st.channels));
        }
        if (p.sections.empty()) p.note = "No senders connected. Click NETWORK on the MELVILLE bar to connect one."; }

    {   auto& p = panel ("pequod", "PEQUOD HOST");
        section (p, "OUT", true, "pequod", { "Out 1", "Out 2" }, lr);
        section (p, "IN", false, "pequod", { "In 1", "In 2" }, lr); }

    {   auto& p = panel ("thoreau", "THOREAU");
        section (p, "OUT", true, "thoreau", { "Out 1", "Out 2" }, lr); }

    {   auto& p = panel ("hawthorne", "HAWTHORNE");
        section (p, "TRACKS", true, "route", numbered ("Hawthorne track ", tracks), numbers (tracks));
        section (p, "MASTER", true, "route", { "Hawthorne master L", "Hawthorne master R" }, lr);
        section (p, "LTC", true, "route", { "Hawthorne LTC" }, { "LTC" });
        section (p, "MONITOR OUT", true, "hawthorne", numbered ("Out ", outs), numbers (outs));
        section (p, "INPUTS", false, "hawthorne", numbered ("In ", ins), numbers (ins));
        section (p, "MELVILLE BUSES", false, "hawthorne", numbered ("MELVILLE bus ", 16), numbers (16));
        section (p, "ROUTE TRACKS", false, "route", numbered ("To Hawthorne track ", tracks), numbers (tracks));
        section (p, "ROUTE MASTER", false, "route", { "To Hawthorne master L", "To Hawthorne master R" }, lr); }

    {   auto& p = panel ("emerson", "EMERSON");
        section (p, "DIRECT OUTS", true, "emerson", numbered ("To Hawthorne track ", tracks), numbers (tracks));
        section (p, "MIX OUT", true, "emerson", { "To Hawthorne master L", "To Hawthorne master R" }, lr);
        section (p, "TO DESK", true, "link", numbered ("Hawthorne track ", tracks), numbers (tracks));
        section (p, "RETURN A", true, "link", { "EMERSON return A L", "EMERSON return A R" }, lr);
        section (p, "RETURN B", true, "link", { "EMERSON return B L", "EMERSON return B R" }, lr);
        section (p, "TRACKS IN", false, "emerson", numbered ("Hawthorne track ", tracks), numbers (tracks));
        section (p, "MASTER IN", false, "emerson", { "Hawthorne master L", "Hawthorne master R" }, lr);
        section (p, "LTC", false, "emerson", { "Hawthorne LTC" }, { "LTC" });
        section (p, "DESK MIX", false, "link", { "To Hawthorne master L", "To Hawthorne master R" }, lr);
        section (p, "SEND A", false, "link", { "To EMERSON send A L", "To EMERSON send A R" }, lr);
        section (p, "SEND B", false, "link", { "To EMERSON send B L", "To EMERSON send B R" }, lr); }

    {   auto& p = panel ("whitman", "WHITMAN");
        section (p, "MIX", true, "whitman", { "To Hawthorne master L", "To Hawthorne master R" }, lr);
        section (p, "SEND A", true, "whitman", { "To EMERSON send A L", "To EMERSON send A R" }, lr);
        section (p, "SEND B", true, "whitman", { "To EMERSON send B L", "To EMERSON send B R" }, lr);
        section (p, "TRACKS", false, "whitman", numbered ("Hawthorne track ", tracks), numbers (tracks));
        section (p, "RETURN A", false, "whitman", { "EMERSON return A L", "EMERSON return A R" }, lr);
        section (p, "RETURN B", false, "whitman", { "EMERSON return B L", "EMERSON return B R" }, lr); }

    layOut();
    repaint();
}

void PatchCanvas::setShowEveryCable (bool b)
{
    showEveryCable = b;
    repaint();
}

juce::String PatchCanvas::networkKey() const
{
    juce::String key;
    auto& rx = hub.getNetwork().getReceiver();
    for (int slot = 0; slot < NetworkNode::slots; ++slot)
    {
        const auto st = rx.getStatus (slot);
        if (st.state != hawthorne::NetReceiver::State::off) key << slot << st.name << st.channels << "|";
    }
    return key;
}

void PatchCanvas::timerCallback()
{
    int ins = 8, outs = 8;
    if (auto* dev = hub.deviceManager.getCurrentAudioDevice())
    {
        ins = juce::jlimit (2, 64, dev->getActiveInputChannels().countNumberOfSetBits());
        outs = juce::jlimit (2, 64, dev->getActiveOutputChannels().countNumberOfSetBits());
    }
    if (hub.getVisibleTracks() != lastTracks || ins != lastIns || outs != lastOuts || networkKey() != lastNetwork)
        rebuild();
}

//==============================================================================
void PatchCanvas::layOut()
{
    const float width = (float) juce::jmax (700, getWidth());
    float y = margin;
    for (auto& p : panels)
    {
        const float left = margin + nameColumn, right = width - margin - panelPad;
        float panelTop = y;
        y += panelPad;
        // outputs on top, inputs below; each wraps onto more lines if it must
        for (int row = 0; row < 2; ++row)
        {
            const bool outputs = row == 0;
            bool any = false;
            float x = left;
            for (auto& s : p.sections)
            {
                if (s.output != outputs) continue;
                const float w = juce::jmax ((float) s.jacks.size() * jackPitch, juce::GlyphArrangement::getStringWidth (font (11.0f, true), s.title) + 4.0f);
                if (any && x + w > right) { x = left; y += rowH; }
                s.titleArea = { x, y, w, titleH };
                s.area = { x, y, w, rowH };
                for (size_t k = 0; k < s.jacks.size(); ++k)
                    s.jacks[k].centre = { x + jackPitch * ((float) k + 0.5f), y + titleH + jackPitch * 0.5f };
                x += w + sectionGap;
                any = true;
            }
            if (any) y += rowH;
        }
        if (p.sections.empty() && p.note.isNotEmpty()) y += rowH * 0.6f;
        y += panelPad - 6.0f;
        p.area = { margin, panelTop, width - margin * 2.0f, y - panelTop };
        y += panelGap;
    }
    contentHeight = (int) y + 40;
}

PatchCanvas::Hit PatchCanvas::hitTest (juce::Point<float> pt) const
{
    for (int pi = 0; pi < (int) panels.size(); ++pi)
    {
        const auto& p = panels[(size_t) pi];
        if (! p.area.expanded (4.0f).contains (pt)) continue;
        for (int si = 0; si < (int) p.sections.size(); ++si)
        {
            const auto& s = p.sections[(size_t) si];
            if (s.titleArea.expanded (2.0f).contains (pt)) return { pi, si, -1, true };
            for (int ji = 0; ji < (int) s.jacks.size(); ++ji)
                if (s.jacks[(size_t) ji].centre.getDistanceFrom (pt) <= jackPitch * 0.5f)
                    return { pi, si, ji, false };
        }
    }
    return {};
}

const PatchCanvas::Jack* PatchCanvas::jackFor (int flat, bool output) const
{
    for (auto& p : panels)
        for (auto& s : p.sections)
            if (s.output == output)
                for (auto& j : s.jacks)
                    if (j.flat == flat) return &j;
    return nullptr;
}

const PatchCanvas::Section* PatchCanvas::sectionFor (int flat, bool output, int* panelIndex) const
{
    for (int pi = 0; pi < (int) panels.size(); ++pi)
        for (auto& s : panels[(size_t) pi].sections)
            if (s.output == output)
                for (auto& j : s.jacks)
                    if (j.flat == flat) { if (panelIndex != nullptr) *panelIndex = pi; return &s; }
    return nullptr;
}

juce::Colour PatchCanvas::colourOfSource (int flat) const
{
    int pi = -1;
    if (sectionFor (flat, true, &pi) != nullptr) return panels[(size_t) pi].colour;
    return Colours::accent;
}

//==============================================================================
void PatchCanvas::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    for (auto& p : panels) drawPanel (g, p);
    drawCables (g);

    if (drag.active)
        drawCable (g, drag.start, drag.end, drag.sources.empty() ? Colours::accent : colourOfSource (drag.sources.front()),
                   drag.fromTitle ? 7.0f : 3.5f);
}

void PatchCanvas::drawPanel (juce::Graphics& g, const Panel& p) const
{
    const auto face = p.colour;
    const auto ink = face.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1d1b18) : juce::Colour (0xfff2ede2);
    const auto inkDim = ink.withAlpha (0.65f);

    // the faceplate, with rack screws
    g.setColour (face);
    g.fillRoundedRectangle (p.area, 4.0f);
    g.setColour (face.darker (0.35f));
    g.drawRoundedRectangle (p.area.reduced (0.5f), 4.0f, 1.0f);
    for (auto corner : { p.area.getTopLeft() + juce::Point<float> (9, 9), p.area.getBottomLeft() + juce::Point<float> (9, -9),
                         p.area.getTopRight() + juce::Point<float> (-9, 9), p.area.getBottomRight() + juce::Point<float> (-9, -9) })
    {
        g.setColour (face.darker (0.45f));
        g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (corner));
        g.setColour (face.brighter (0.3f));
        g.drawLine (corner.x - 2.5f, corner.y, corner.x + 2.5f, corner.y, 1.0f);
    }

    // the name
    auto nameArea = juce::Rectangle<float> (p.area.getX() + 20.0f, p.area.getY(), nameColumn - 28.0f, p.area.getHeight());
    g.setColour (ink);
    g.setFont (font (16.0f, true));
    g.drawFittedText (p.title, nameArea.toNearestInt(), juce::Justification::centredLeft, 2);
    g.setColour (face.darker (0.25f));
    g.drawVerticalLine ((int) (p.area.getX() + nameColumn - 4.0f), p.area.getY() + 8.0f, p.area.getBottom() - 8.0f);

    if (p.note.isNotEmpty())
    {
        g.setColour (inkDim);
        g.setFont (font (12.0f));
        g.drawText (p.note, p.area.withTrimmedLeft (nameColumn + 4.0f).reduced (0, 4), juce::Justification::centredLeft);
    }

    // OUT / IN marks at the start of each row
    for (auto& s : p.sections)
    {
        if (s.titleArea.getX() > p.area.getX() + nameColumn + margin + 1.0f) continue;   // only a row's first section
        g.setColour (inkDim);
        g.setFont (font (10.0f, true));
        g.drawText (s.output ? "OUT" : "IN", juce::Rectangle<float> (p.area.getX() + nameColumn - 34.0f, s.titleArea.getY() + titleH, 26.0f, jackPitch),
                    juce::Justification::centredRight);
    }

    for (int si = 0; si < (int) p.sections.size(); ++si)
    {
        const auto& s = p.sections[(size_t) si];
        const bool hoverTitle = hover.titleOnly && &panels[(size_t) hover.panel] == &p && hover.section == si;

        // the section's name, with a bracket over its jacks
        g.setColour (hoverTitle ? ink : inkDim);
        g.setFont (font (11.0f, true));
        g.drawText (s.title, s.titleArea, juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.25f));
        g.drawHorizontalLine ((int) s.titleArea.getBottom() - 1, s.titleArea.getX(), s.titleArea.getX() + (float) s.jacks.size() * jackPitch - 4.0f);

        for (int ji = 0; ji < (int) s.jacks.size(); ++ji)
        {
            const auto& j = s.jacks[(size_t) ji];
            const bool lit = ! hover.titleOnly && hover.jack == ji && hover.section == si && &panels[(size_t) hover.panel] == &p;
            auto r = juce::Rectangle<float> (jackRadius * 2.0f, jackRadius * 2.0f).withCentre (j.centre);
            g.setColour (face.darker (0.5f));
            g.fillEllipse (r.expanded (2.5f));                 // the nut
            g.setColour (juce::Colour (0xff141414));
            g.fillEllipse (r);                                 // the hole
            g.setColour (lit ? Colours::accent : face.darker (0.2f));
            g.drawEllipse (r.expanded (2.5f), lit ? 2.0f : 1.0f);

            g.setColour (inkDim);
            g.setFont (font (9.5f));
            g.drawText (j.label, juce::Rectangle<float> (jackPitch + 6.0f, labelH).withCentre ({ j.centre.x, j.centre.y + jackPitch * 0.5f + labelH * 0.5f }),
                        juce::Justification::centred, false);
        }
    }
}

void PatchCanvas::drawCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c, float thickness)
{
    // a cable hangs: the further apart the plugs, the deeper the sag
    const float sag = 30.0f + 0.22f * a.getDistanceFrom (b);
    juce::Path path;
    path.startNewSubPath (a);
    path.cubicTo (a.translated (0, sag), b.translated (0, sag), b);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (path, juce::PathStrokeType (thickness + 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // the plugs
    for (auto p : { a, b })
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillEllipse (juce::Rectangle<float> (thickness + 7.0f, thickness + 7.0f).withCentre (p));
        g.setColour (c.brighter (0.15f));
        g.fillEllipse (juce::Rectangle<float> (thickness + 4.0f, thickness + 4.0f).withCentre (p));
    }
}

void PatchCanvas::drawCables (juce::Graphics& g) const
{
    multicores.clear();
    struct Group { std::vector<std::pair<juce::Point<float>, juce::Point<float>>> ends; juce::Colour colour; };
    std::map<std::tuple<int, int, int, int>, Group> groups;

    for (auto& [d, cord] : hub.getPatchbay().getAllCords())
    {
        int sp = -1, dp = -1;
        const auto* ss = sectionFor (cord.source, true, &sp);
        const auto* ds = sectionFor (d, false, &dp);
        const auto* sj = jackFor (cord.source, true);
        const auto* dj = jackFor (d, false);
        if (ss == nullptr || ds == nullptr || sj == nullptr || dj == nullptr) continue;   // not on a panel (beyond the reel, say)
        const int ssi = (int) (ss - panels[(size_t) sp].sections.data());
        const int dsi = (int) (ds - panels[(size_t) dp].sections.data());
        auto& grp = groups[{ sp, ssi, dp, dsi }];
        grp.colour = panels[(size_t) sp].colour;
        grp.ends.push_back ({ sj->centre, dj->centre });
    }

    for (auto& [key, grp] : groups)
    {
        if (showEveryCable || grp.ends.size() < 3)
        {
            for (auto& [a, b] : grp.ends) drawCable (g, a, b, grp.colour, 3.5f);
            continue;
        }
        // a multicore: from the middle of the jacks it leaves to the middle of those it reaches
        juce::Point<float> a, b;
        for (auto& [x, y] : grp.ends) { a += x; b += y; }
        a /= (float) grp.ends.size();
        b /= (float) grp.ends.size();
        drawCable (g, a, b, grp.colour, 8.0f);

        const float sag = 30.0f + 0.22f * a.getDistanceFrom (b);
        const auto mid = (a + b) * 0.5f + juce::Point<float> (0, sag * 0.75f);
        auto tag = juce::Rectangle<float> (34.0f, 18.0f).withCentre (mid);
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (tag, 9.0f);
        g.setColour (grp.colour);
        g.drawRoundedRectangle (tag, 9.0f, 1.5f);
        g.setFont (font (11.0f, true));
        g.drawText (juce::String ((int) grp.ends.size()), tag, juce::Justification::centred);
        multicores.push_back ({ std::get<0> (key), std::get<1> (key), std::get<2> (key), std::get<3> (key), mid });
    }
}

//==============================================================================
void PatchCanvas::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    if (h.panel != hover.panel || h.section != hover.section || h.jack != hover.jack || h.titleOnly != hover.titleOnly)
    {
        hover = h;
        juce::String tip;
        if (h.panel >= 0)
        {
            const auto& s = panels[(size_t) h.panel].sections[(size_t) h.section];
            auto& pb = hub.getPatchbay();
            if (h.titleOnly)
                tip = s.output ? "Drag onto an input section to patch all " + juce::String ((int) s.jacks.size()) + " in order"
                               : "Drop an output section here to patch it in order";
            else
            {
                const int flat = s.jacks[(size_t) h.jack].flat;
                tip = s.output ? pb.sourceName (flat) : pb.destName (flat);
                int n = 0;
                for (auto& [d, c] : pb.getAllCords())
                    if (s.output ? c.source == flat : d == flat) ++n;
                if (n > 0) tip << "  (" << n << (n == 1 ? " cable)" : " cables)");
            }
        }
        setTooltip (tip);
        setMouseCursor (h.panel >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PatchCanvas::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void PatchCanvas::mouseDown (const juce::MouseEvent& e)
{
    drag = {};
    const auto h = hitTest (e.position);

    if (h.panel < 0)
    {
        // a multicore's tag
        for (auto& m : multicores)
            if (m.label.getDistanceFrom (e.position) < 14.0f) { showMulticoreMenu (m.sp, m.ss, m.dp, m.ds); return; }
        return;
    }

    if (e.mods.isPopupMenu()) { if (! h.titleOnly) showJackMenu (h); return; }

    const auto& s = panels[(size_t) h.panel].sections[(size_t) h.section];
    auto& pb = hub.getPatchbay();
    drag.from = h;
    drag.end = e.position;

    if (h.titleOnly)
    {
        if (! s.output) return;
        drag.fromTitle = true;
        for (auto& j : s.jacks) drag.sources.push_back (j.flat);
        drag.start = { s.titleArea.getCentreX(), s.titleArea.getBottom() };
    }
    else if (s.output)
    {
        drag.sources.push_back (s.jacks[(size_t) h.jack].flat);
        drag.start = s.jacks[(size_t) h.jack].centre;
    }
    else
    {
        // pulling the plug out of an input: it comes away on its cable(s)
        const int dest = s.jacks[(size_t) h.jack].flat;
        for (auto& [d, c] : pb.getAllCords())
            if (d == dest) drag.sources.push_back (c.source);
        if (drag.sources.empty())
        {
            drag.start = s.jacks[(size_t) h.jack].centre;     // an empty input: drag to an output
        }
        else
        {
            for (int src : drag.sources) pb.disconnect (src, dest);
            if (auto* j = jackFor (drag.sources.front(), true)) drag.start = j->centre;
        }
    }
    drag.active = true;
    repaint();
}

void PatchCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.active) return;
    drag.end = e.position;
    hover = hitTest (e.position);
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        vp->autoScroll (e.getEventRelativeTo (vp).x, e.getEventRelativeTo (vp).y, 30, 12);
    repaint();
}

void PatchCanvas::mouseUp (const juce::MouseEvent& e)
{
    if (! drag.active) return;
    const auto to = hitTest (e.position);
    auto& pb = hub.getPatchbay();
    const auto& fromSection = panels[(size_t) drag.from.panel].sections[(size_t) drag.from.section];

    if (to.panel >= 0)
    {
        const auto& s = panels[(size_t) to.panel].sections[(size_t) to.section];
        if (! fromSection.output && drag.sources.empty() && s.output && ! to.titleOnly)
        {
            // dragged from an empty input to an output
            pb.connect (s.jacks[(size_t) to.jack].flat, fromSection.jacks[(size_t) drag.from.jack].flat);
        }
        else if (! s.output && ! drag.sources.empty())
        {
            if (drag.fromTitle)
            {
                // a whole section, in order, from the jack it was dropped on
                const int first = to.titleOnly ? 0 : to.jack;
                for (size_t k = 0; k < drag.sources.size() && first + (int) k < (int) s.jacks.size(); ++k)
                    pb.connect (drag.sources[k], s.jacks[(size_t) first + k].flat);
            }
            else if (! to.titleOnly)
            {
                for (int src : drag.sources) pb.connect (src, s.jacks[(size_t) to.jack].flat);
            }
        }
    }
    // dropped anywhere else: a plug pulled from an input stays out
    drag = {};
    repaint();
}

//==============================================================================
void PatchCanvas::showJackMenu (const Hit& h)
{
    const auto& s = panels[(size_t) h.panel].sections[(size_t) h.section];
    const int flat = s.jacks[(size_t) h.jack].flat;
    auto& pb = hub.getPatchbay();

    std::vector<std::pair<int, Cord>> cords;    // (dest, cord)
    for (auto& dc : pb.getAllCords())
        if (s.output ? dc.second.source == flat : dc.first == flat) cords.push_back (dc);

    juce::PopupMenu m;
    m.addSectionHeader (s.output ? pb.sourceName (flat) : pb.destName (flat));
    if (cords.empty()) m.addItem (-1, "No cables", false);

    const std::pair<const char*, float> levels[] = { { "0 dB", 1.0f }, { "-3 dB", 0.7079f }, { "-6 dB", 0.5012f }, { "-12 dB", 0.2512f }, { "-18 dB", 0.1259f } };
    for (size_t i = 0; i < cords.size(); ++i)
    {
        const auto& [d, c] = cords[i];
        juce::PopupMenu sub;
        for (int k = 0; k < 5; ++k)
            sub.addItem ((int) i * 100 + 10 + k, levels[k].first, true, std::abs (c.gain - levels[k].second) < 0.01f);
        sub.addSeparator();
        sub.addItem ((int) i * 100 + 99, "Pull this cable");
        m.addSubMenu (s.output ? pb.destName (d) : pb.sourceName (c.source), sub);
    }
    if (! cords.empty())
    {
        m.addSeparator();
        m.addItem (1, "Pull every cable here");
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [this, cords, levels] (int r)
                     {
                         auto& p = hub.getPatchbay();
                         if (r == 1) { for (auto& [d, c] : cords) p.disconnect (c.source, d); return; }
                         if (r < 10) return;
                         const auto& [d, c] = cords[(size_t) (r / 100)];
                         const int k = r % 100;
                         if (k == 99) p.disconnect (c.source, d);
                         else if (k >= 10 && k < 15) p.connect (c.source, d, levels[k - 10].second);
                     });
}

void PatchCanvas::showMulticoreMenu (int sp, int ss, int dp, int ds)
{
    const auto& from = panels[(size_t) sp].sections[(size_t) ss];
    const auto& to = panels[(size_t) dp].sections[(size_t) ds];
    juce::PopupMenu m;
    m.addSectionHeader (panels[(size_t) sp].title + " " + from.title + "  ->  " + panels[(size_t) dp].title + " " + to.title);
    m.addItem (1, "Pull the whole multicore");
    m.addItem (2, "Show every cable");
    std::vector<int> srcs, dsts;
    for (auto& j : from.jacks) srcs.push_back (j.flat);
    for (auto& j : to.jacks) dsts.push_back (j.flat);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [this, srcs, dsts] (int r)
                     {
                         if (r == 1)
                         {
                             auto& p = hub.getPatchbay();
                             for (int d : dsts) for (int s : srcs) if (p.isConnected (s, d)) p.disconnect (s, d);
                         }
                         else if (r == 2)
                         {
                             setShowEveryCable (true);
                             if (onShowEveryCable) onShowEveryCable();
                         }
                     });
}

//==============================================================================
PatchbayView::PatchbayView (Hub& h) : hub (h), canvas (h)
{
    defaultButton.setTooltip ("Put every cable back the way MELVILLE starts: interface > Hawthorne > EMERSON > WHITMAN > back to Hawthorne");
    defaultButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Default patch",
            "Replace every cable with MELVILLE's default patch?", "Replace", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (ok) hub.defaultPatch(); }));
    };
    clearButton.setTooltip ("Pull every cable out of the patchbay");
    clearButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Pull every cable",
            "Pull every cable out? Nothing will reach anything until you patch it again.", "Pull them", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (ok) hub.getPatchbay().clearAll(); }));
    };
    saveButton.onClick = [this] { save(); };
    openButton.onClick = [this] { open(); };
    everyCable.setTooltip ("Draw every cable on its own instead of runs as multicores");
    everyCable.setColour (juce::ToggleButton::textColourId, Colours::text);
    everyCable.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    everyCable.onClick = [this] { canvas.setShowEveryCable (everyCable.getToggleState()); };
    canvas.onShowEveryCable = [this] { everyCable.setToggleState (true, juce::dontSendNotification); };
    for (auto* b : { &defaultButton, &clearButton, &saveButton, &openButton }) addAndMakeVisible (b);
    addAndMakeVisible (everyCable);

    viewport.setViewedComponent (&canvas, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setSize (1280, 860);
}

PatchbayView::~PatchbayView() = default;

void PatchbayView::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    auto bar = getLocalBounds().removeFromTop (46);
    g.setColour (Colours::panel);
    g.fillRect (bar);
    g.setColour (Colours::textDim);
    g.setFont (font (12.0f));
    g.drawText ("Drag from an OUT jack to an IN jack. Drag a plug out of an IN jack to move or pull it. Drag a section's name to patch the whole section.",
                bar.reduced (14, 0).withTrimmedRight (520), juce::Justification::centredLeft, true);
}

void PatchbayView::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (46).reduced (10, 9);
    for (auto* b : { &openButton, &saveButton, &clearButton, &defaultButton })
    {
        b->setBounds (bar.removeFromRight (b == &clearButton ? 96 : 80));
        bar.removeFromRight (6);
    }
    everyCable.setBounds (bar.removeFromRight (170));
    viewport.setBounds (r);

    const int w = viewport.getMaximumVisibleWidth();
    canvas.setSize (w, canvas.getHeight());
    canvas.rebuild();
    canvas.setSize (w, juce::jmax (canvas.getContentHeight(), viewport.getMaximumVisibleHeight()));
}

void PatchbayView::save()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.melpatch");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              hub.getPatchbay().toXml()->writeTo (f.withFileExtension ("melpatch"));
                          });
}

void PatchbayView::open()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.melpatch");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (auto xml = juce::parseXML (fc.getResult()))
                                  if (xml->hasTagName ("PATCH")) hub.getPatchbay().fromXml (*xml);
                          });
}

} // namespace melville
