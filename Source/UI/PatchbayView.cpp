#include "PatchbayView.h"
#include "MelvilleLookAndFeel.h"
#include "../Core/NetworkNode.h"
#include "../Core/Bridges.h"
#include "MelvilleHosted.h"
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

    // One side of a column: a section of jacks on one port
    struct Side
    {
        juce::String title, portId, description;
        juce::StringArray channels, labels;
    };

    auto makeSection = [&pb] (const Side& side, bool output, int column) -> Section
    {
        Section s;
        s.title = side.title;
        s.description = side.description;
        s.output = output;
        s.column = column;
        for (int i = 0; i < side.channels.size(); ++i)
        {
            const int flat = output ? pb.findSource (side.portId, side.channels[i]) : pb.findDest (side.portId, side.channels[i]);
            if (flat < 0) continue;
            Jack j;
            j.flat = flat;
            j.label = side.labels[i];
            j.midi = side.portId == "midi";
            {
                // a tape track ("Track 7", "Hawthorne track 7", "To WHITMAN track 7" ...): its name and colour from the reel
                const auto nm = side.channels[i];
                if (nm.containsIgnoreCase ("track ") && nm.getTrailingIntValue() > 0
                    && nm.fromLastOccurrenceOf (" ", false, false).containsOnly ("0123456789"))
                    j.track = nm.getTrailingIntValue() - 1;
            }
            s.jacks.push_back (j);
        }
        return s;
    };

    // A column: an output section on top and the input it pairs with underneath, jack over jack
    auto column = [&] (Panel& p, const Side& topSide, const Side& bottomSide)
    {
        // the AUDIO tab shows the audio jacks, the MIDI tab the MIDI ones (same panels, same order)
        const Side none;
        const auto& top = (topSide.portId == "midi") == midiView ? topSide : none;
        const auto& bottom = (bottomSide.portId == "midi") == midiView ? bottomSide : none;
        if (top.channels.isEmpty() && bottom.channels.isEmpty()) return;
        const int c = p.columns++;
        if (top.channels.size() > 0)    { auto s = makeSection (top, true, c);     if (! s.jacks.empty()) p.sections.push_back (std::move (s)); }
        if (bottom.channels.size() > 0) { auto s = makeSection (bottom, false, c); if (! s.jacks.empty()) p.sections.push_back (std::move (s)); }
    };
    const Side none;

    const juce::StringArray lr { "L", "R" };

    {   // the control room
        auto& p = panel ("twain", "TWAIN");
        const juce::StringArray abc { "A", "B", "C" };
        juce::StringArray spk, spkShort;
        for (auto s : abc) { spk.add ("Speakers " + s + " L"); spk.add ("Speakers " + s + " R"); spkShort.add (s + " L"); spkShort.add (s + " R"); }
        column (p, { "SPEAKERS", "twain", "The control room's monitor out, on the speaker set chosen in TWAIN (A, B or C): to the interface outputs your speakers are on.",
                     spk, spkShort },
                   { "MIX IN", "twain", "The mix (WHITMAN's, after EMERSON's MIX chain by default): a monitor source and in the headphone feeds.",
                     { "Mix L", "Mix R" }, lr });
        column (p, none, { "2-TRACK IN", "twain", "Hawthorne's MASTER OUT: the master tape playing back (while you print, the mix going onto it). A monitor source: 2-TRACK in TWAIN.", { "2-track L", "2-track R" }, lr });
        column (p, none, { "EXT 1 IN", "twain", "Anything else to listen to (a phone, a reference): a monitor source.", { "Ext 1 L", "Ext 1 R" }, lr });
        column (p, none, { "EXT 2 IN", "twain", "Anything else to listen to (a phone, a reference): a monitor source.", { "Ext 2 L", "Ext 2 R" }, lr });
        juce::StringArray ph, phShort;
        for (int f = 1; f <= TwainNode::numFeeds; ++f)
        {
            ph.add ("Phones " + juce::String (f) + " L"); ph.add ("Phones " + juce::String (f) + " R");
            phShort.add (juce::String (f) + "L"); phShort.add (juce::String (f) + "R");
        }
        column (p, { "PHONES", "twain", "The players' headphone feeds, each its own mix (TWAIN): to the interface outputs the headphone amps are on.", ph, phShort },
                   { "CLICK / TALK", "twain", "The click (into the headphone feeds) and the talkback mic (into them while TALK is held).",
                     { "Click", "Talkback" }, { "CLK", "TB" } });
        column (p, none, { "CUE IN", "twain", "What the players can have in their headphones: cable tracks, stems, THOREAU or IRVING here and set their levels per feed in TWAIN.",
                           numbered ("Cue ", TwainNode::numCues), numbers (TwainNode::numCues) });
    }

    {   auto& p = panel ("interface", "INTERFACE");
        column (p, { "INPUTS", "interface", "What comes into your audio interface (mics, instruments, line inputs). Cable it to whatever should hear it.",
                     numbered ("In ", ins), numbers (ins) },
                   { "OUTPUTS", "interface", "Your audio interface's outputs (monitors, headphones, outboard, a hardware desk).",
                     numbered ("Out ", outs), numbers (outs) }); }

    {   // ... and its MIDI side: the keyboards and MIDI interface (5-pin DIN; one cable carries all sixteen channels)
        auto& p = panels.back();
        if (const auto* mp = pb.findPort ("midi"))
        {
            juce::StringArray ins, inShort, outs, outShort;
            int k = 0;
            for (auto& nm : mp->sources)
                if (nm == "Keyboards" || nm.startsWith ("In: ")) { ins.add (nm); inShort.add (nm == "Keyboards" ? "ALL" : juce::String (++k)); }
            k = 0;
            for (auto& nm : mp->dests)
                if (nm.startsWith ("Out: ")) { outs.add (nm); outShort.add (juce::String (++k)); }
            column (p, { "MIDI IN", "midi", "Your MIDI keyboards and controllers: ALL is every one of them at once, then each on its own ("
                                            + ins.joinIntoString (", ").replace ("In: ", "") + "). A MIDI device opens while something's cabled to it.",
                         ins, inShort },
                       { "MIDI OUT", "midi", outs.isEmpty() ? juce::String ("Your MIDI interface's outputs (none found): to drum machines, synths and sequencers.")
                                                            : "Your MIDI outputs (" + outs.joinIntoString (", ").replace ("Out: ", "") + "): to drum machines, synths and sequencers.",
                         outs, outShort });
        }
    }

    {   // MELVILLE itself: the transport's timecode and click
        auto& p = panel ("melville", "MELVILLE");
        column (p, { "LTC OUT", "clock", "MELVILLE's timecode (LTC) from the transport, for anything that chases it (CLOCK > TIMECODE).", { "LTC" }, { "LTC" } },
                   { "LTC IN", "hawthorne", "Timecode for MELVILLE's transport to chase (CLOCK > TIMECODE > chase LTC).", { "LTC in" }, { "LTC" } });
        column (p, { "CLICK", "clock", "The click (metronome) at MELVILLE's tempo: to the monitors by default (CLOCK > CLICK).", { "Click" }, { "CLK" } }, none);
        column (p, { "MIDI CLOCK", "midi", "MIDI clock at MELVILLE's tempo, 24 a beat, with start, stop, continue and song position as the transport "
                                          "rolls and locates: to a MIDI OUT for a drum machine or sequencer to follow, or to ALCOTT for its arpeggiators.",
                     { "Clock" }, { "CLK" } }, none);

        // MELVILLE Link, to the right: a set of jacks per connected sender (THOREAU, IRVING ... on other PCs)
        auto& rx = hub.getNetwork().getReceiver();
        for (int slot = 0; slot < NetworkNode::slots; ++slot)
        {
            const auto st = rx.getStatus (slot);
            if (st.state == hawthorne::NetReceiver::State::off || st.channels <= 0) continue;
            juce::StringArray chans;
            for (int c = 0; c < st.channels; ++c) chans.add (NetworkNode::channelName (slot, c));
            column (p, { "LINK " + juce::String (slot + 1) + " " + st.name.toUpperCase(), "network",
                         "Audio from " + st.name + " on another PC, over MELVILLE Link. Senders follow the transport's position and tempo.",
                         chans, numbers (st.channels) }, none);
        }
    }

    {   // IRVING: one panel, a set of jacks per instance open
        auto& p = panel ("pequod", "IRVING");
        int open = 0;
        for (int n = 1; n <= Hub::maxIrvings; ++n)
            open += (n == 1 || hub.isPartConnected (Hub::irvingId (n))) ? 1 : 0;
        for (int n = 1; n <= Hub::maxIrvings; ++n)
        {
            const auto id = Hub::irvingId (n);
            if (n > 1 && ! hub.isPartConnected (id)) continue;
            const auto tag = open > 1 ? juce::String (n) + " " : juce::String();
            column (p, { tag + "OUT", id, "What you hear from IRVING" + (open > 1 ? " " + juce::String (n) : juce::String()) + "'s rack.", { "Out 1", "Out 2" }, lr },
                       { tag + "IN", id, n == 1 ? juce::String ("Into the guitar rig: your guitar (interface input 1 by default).")
                                                : "Into IRVING " + juce::String (n) + ": cable an interface input (or anything) here.",
                         { "In 1", "In 2" }, lr });
        }
    }

    {   auto& p = panel ("thoreau", "THOREAU");
        column (p, { "MIX OUT", "thoreau", "THOREAU's stereo mix: the whole kit after its mixer.", { "Mix L", "Mix R" }, lr }, none);
        juce::StringArray kit, kitShort;
        const auto all = names::thoreauOuts();
        for (int i = 2; i < all.size(); ++i) kit.add (all[i]);
        for (int i = 1; i <= 12; ++i) kitShort.add (juce::String (i));
        kitShort.addArray ({ "OVL", "OVR", "RML", "RMR" });
        column (p, { "KIT OUTS", "thoreau", "THOREAU's pads 1-12 each on their own, in the order of its pattern rows (pad 1 is the kick), after "
                                            "their levels: cable them to tape tracks or desk channels to record or mix the kit in parts. Then the overheads and room.",
                     kit, kitShort }, none);
        column (p, { "MIDI OUT", "midi", "THOREAU's steps as MIDI, as it plays them: pad 1 on channel 1, pad 2 on channel 2 ..., each with its pad's MIDI note. "
                                        "Cable them to ALCOTT's slots, or a MIDI OUT for a drum module.",
                     { "THOREAU steps" }, { "STEPS" } }, none); }

    {   auto& p = panel ("alcott", "ALCOTT");
        column (p, { "MIX OUT", "alcott", "ALCOTT's mix: every slot together, after its level.", { "Mix L", "Mix R" }, lr }, none);
        juce::StringArray slotOuts, slotShort;
        for (int k = 1; k <= 8; ++k)
        {
            slotOuts.addArray ({ "Slot " + juce::String (k) + " L", "Slot " + juce::String (k) + " R" });
            slotShort.addArray ({ juce::String (k) + "L", juce::String (k) + "R" });
        }
        column (p, { "SLOT OUTS", "alcott", "Each of ALCOTT's slots on its own (its instrument and effects): cable them to tape tracks to record them, "
                                           "or to TWAIN's CUE IN to put them in the headphones.",
                     slotOuts, slotShort }, none);
        juce::StringArray slotIns, slotInShort;
        for (int k = 1; k <= MidiNode::numAlcottSlots; ++k) { slotIns.add ("ALCOTT slot " + juce::String (k)); slotInShort.add (juce::String (k)); }
        column (p, none, { "MIDI IN", "midi", "Each slot's MIDI in: what plays its instrument. A keyboard, THOREAU's steps (pick its pad by the slot's channel), "
                                              "the clock for arpeggiators. Several into one slot merge.",
                           slotIns, slotInShort }); }

    {   auto& p = panel ("hawthorne", "HAWTHORNE");
        column (p, { "TRACK OUT", "route", "Each tape track playing back (or its input, while the track monitors input). To EMERSON by default.",
                     numbered ("Hawthorne track ", tracks), numbers (tracks) },
                   { "TRACK IN", "hawthorne", "What each tape track records: cable a mic, IRVING, THOREAU or a MELVILLE Link sender here.",
                     numbered ("Track ", tracks), numbers (tracks) });
        column (p, { "MASTER OUT", "route", "Hawthorne's master: the 2-track playing back, or what's coming into it. Your monitors (interface outputs 1-2) by default.", { "Hawthorne master L", "Hawthorne master R" }, lr },
                   { "MASTER IN", "route", "What the 2-track master records: the mix (EMERSON's MIX OUT by default).",
                     { "To Hawthorne master L", "To Hawthorne master R" }, lr });
    }

    {   auto& p = panel ("emerson", "EMERSON");
        // (its processed tracks: one set, cabled to WHITMAN, the interface for a hardware desk, or both)
        column (p, { "TRACKS OUT", "link", "Each track after its EMERSON inserts. To WHITMAN by default; cable to interface outputs for a hardware desk.",
                     numbered ("Hawthorne track ", tracks), numbers (tracks) },
                   { "TRACKS IN", "emerson", "The tape tracks coming into EMERSON's inserts (from Hawthorne by default).",
                     numbered ("Hawthorne track ", tracks), numbers (tracks) });
        column (p, { "MIX OUT", "emerson", "The mix after EMERSON's MIX chain: to Hawthorne's 2-track master by default.",
                     { "To Hawthorne master L", "To Hawthorne master R" }, lr },
                   { "MIX IN", "link", "The desk's mix coming into EMERSON's MIX chain (WHITMAN's mix by default).",
                     { "To Hawthorne master L", "To Hawthorne master R" }, lr });
        column (p, { "RETURN A", "link", "FX return A: EMERSON's return A chain coming back to the desk.", { "EMERSON return A L", "EMERSON return A R" }, lr },
                   { "SEND A", "link", "FX send A from the desk into EMERSON's return A chain.", { "To EMERSON send A L", "To EMERSON send A R" }, lr });
        column (p, { "RETURN B", "link", "FX return B: EMERSON's return B chain coming back to the desk.", { "EMERSON return B L", "EMERSON return B R" }, lr },
                   { "SEND B", "link", "FX send B from the desk into EMERSON's return B chain.", { "To EMERSON send B L", "To EMERSON send B R" }, lr }); }

    {   auto& p = panel ("whitman", "WHITMAN");
        column (p, none, { "TRACKS", "whitman", "Into WHITMAN's channel strips, one per track (EMERSON's TRACKS OUT by default).",
                           numbered ("Hawthorne track ", tracks), numbers (tracks) });
        column (p, { "MIX", "whitman", "The desk's stereo mix: through EMERSON's MIX chain to Hawthorne's 2-track by default.",
                     { "To Hawthorne master L", "To Hawthorne master R" }, lr }, none);
        column (p, { "SEND A", "whitman", "The desk's FX send A (to EMERSON's return A by default).", { "To EMERSON send A L", "To EMERSON send A R" }, lr },
                   { "RETURN A", "whitman", "FX return A coming back onto the desk.", { "EMERSON return A L", "EMERSON return A R" }, lr });
        column (p, { "SEND B", "whitman", "The desk's FX send B (to EMERSON's return B by default).", { "To EMERSON send B L", "To EMERSON send B R" }, lr },
                   { "RETURN B", "whitman", "FX return B coming back onto the desk.", { "EMERSON return B L", "EMERSON return B R" }, lr }); }

    for (auto& p : panels)
        if (p.sections.empty() && p.note.isEmpty())
            p.note = midiView ? "No MIDI" : "No audio";

    layOut();
    repaint();
}

void PatchCanvas::setMidiView (bool b)
{
    if (midiView == b) return;
    midiView = b;
    hover = {};
    rebuild();
}

void PatchCanvas::setShowNormals (bool b)
{
    showNormals = b;
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
    for (int n = 2; n <= Hub::maxIrvings; ++n)                 // (IRVINGs opened or removed)
        key << (hub.isPartConnected (Hub::irvingId (n)) ? "I" : "-");
    return key;
}

void PatchCanvas::timerCallback()
{
    if (const int rev = melville::tracks().rev.load(); rev != tracksRev) { tracksRev = rev; repaint(); }   // (track colours)
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
    // Like a hardware patchbay: each column has its outputs right over the inputs they pair
    // with, jack over jack. Long sections (up to 96 tape tracks) fold into lines of 24, each
    // line of outputs over its line of inputs (1-24, then 25-48 ...).
    constexpr int perLine = 24;
    const float width = (float) juce::jmax (700, getWidth());
    const auto titleFont = font (11.0f, true);
    float y = margin;
    for (auto& p : panels)
    {
        const float left = margin + nameColumn, right = width - margin - panelPad;
        const float panelTop = y;
        bool anyOut = false, anyIn = false;
        for (auto& s : p.sections) (s.output ? anyOut : anyIn) = true;

        y += panelPad;
        float x = left, lineH = 0.0f;
        bool lineUsed = false;
        for (int c = 0; c < p.columns; ++c)
        {
            int most = 0;
            float titleW = 0.0f;
            bool colOut = false, colIn = false;
            for (auto& s : p.sections)
                if (s.column == c)
                {
                    (s.output ? colOut : colIn) = true;
                    most = juce::jmax (most, (int) s.jacks.size());
                    titleW = juce::jmax (titleW, juce::GlyphArrangement::getStringWidth (titleFont, s.title) + 4.0f);
                }
            if (most == 0) continue;
            const int across = juce::jmin (most, perLine);
            const int lines = (most + perLine - 1) / perLine;
            const float w = juce::jmax ((float) across * jackPitch, titleW);
            // a column with only inputs (or only outputs) folds without empty rows between
            const float colPair = rowH * (float) ((colOut ? 1 : 0) + (colIn ? 1 : 0));
            const float firstRow = ! colOut && anyOut ? rowH : 0.0f;
            const float h = firstRow + colPair * (float) lines + 6.0f * (float) (lines - 1);

            if (lineUsed && x + w > right) { x = left; y += lineH + 6.0f; lineH = 0.0f; }
            for (auto& s : p.sections)
            {
                if (s.column != c) continue;
                const float sy = y + (! s.output && anyOut ? rowH : 0.0f);
                s.titleArea = { x, sy, w, titleH };
                s.area = { x, sy, w, h };
                for (size_t k = 0; k < s.jacks.size(); ++k)
                {
                    const int line = (int) k / perLine, at = (int) k % perLine;
                    s.jacks[k].centre = { x + jackPitch * ((float) at + 0.5f),
                                          sy + (colPair + 6.0f) * (float) line + titleH + jackPitch * 0.5f };
                }
            }
            x += w + sectionGap;
            lineH = juce::jmax (lineH, h);
            lineUsed = true;
        }
        y += lineUsed ? lineH : rowH * 0.6f;
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
    {
        // which jacks are normalled (0 no, 1 broken, 2 carrying), once per paint
        auto& pb = hub.getPatchbay();
        srcNormalMark.assign ((size_t) pb.getNumSources(), 0);
        dstNormalMark.assign ((size_t) pb.getNumDests(), 0);
        for (auto& [d, src] : pb.getAllNormals())
        {
            const char m = pb.isNormalLive (d) ? 2 : 1;
            dstNormalMark[(size_t) d] = m;
            srcNormalMark[(size_t) src] = juce::jmax (srcNormalMark[(size_t) src], m);
        }
    }
    for (auto& p : panels) drawPanel (g, p);
    if (showNormals) drawNormals (g);
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
    juce::Array<float> marked;
    for (auto& s : p.sections)
        for (size_t k = 0; k < s.jacks.size(); k += 24)    // once per row of jacks
        {
            const float rowY = s.jacks[k].centre.y;
            if (marked.contains (rowY)) continue;
            marked.add (rowY);
            g.setColour (inkDim);
            g.setFont (font (10.0f, true));
            g.drawText (s.output ? "OUT" : "IN", juce::Rectangle<float> (p.area.getX() + nameColumn - 34.0f, rowY - jackPitch * 0.5f, 26.0f, jackPitch),
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
        g.drawHorizontalLine ((int) s.titleArea.getBottom() - 1, s.titleArea.getX(), s.titleArea.getX() + (float) juce::jmin ((int) s.jacks.size(), 24) * jackPitch - 4.0f);

        for (int ji = 0; ji < (int) s.jacks.size(); ++ji)
        {
            const auto& j = s.jacks[(size_t) ji];
            const bool lit = ! hover.titleOnly && hover.jack == ji && hover.section == si && &panels[(size_t) hover.panel] == &p;
            auto r = juce::Rectangle<float> (jackRadius * 2.0f, jackRadius * 2.0f).withCentre (j.centre);
            if (j.midi)
            {
                // a 5-pin DIN socket: metal ring, black insert, five pins in a half circle, the key notch
                const auto din = r.expanded (2.0f);
                g.setColour (juce::Colour (0xffb9b6ae));
                g.fillEllipse (din.expanded (1.5f));
                g.setColour (juce::Colour (0xff141414));
                g.fillEllipse (din);
                g.setColour (juce::Colour (0xffd9d4c7));
                for (int k = 0; k < 5; ++k)
                {
                    const float a = juce::MathConstants<float>::pi * (0.75f + 0.125f * (float) k * 2.0f);   // 135..315 degrees, the top half
                    const auto pin = j.centre + juce::Point<float> (std::cos (a), std::sin (a)) * (din.getWidth() * 0.3f);
                    g.fillEllipse (juce::Rectangle<float> (1.8f, 1.8f).withCentre (pin));
                }
                g.fillRect (juce::Rectangle<float> (2.0f, 2.5f).withCentre ({ j.centre.x, din.getBottom() - 1.8f }));
                g.setColour (lit ? Colours::accent : face.darker (0.2f));
                g.drawEllipse (din.expanded (1.5f), lit ? 2.0f : 1.0f);
            }
            else
            {
            g.setColour (face.darker (0.5f));
            g.fillEllipse (r.expanded (2.5f));                 // the nut
            g.setColour (juce::Colour (0xff141414));
            g.fillEllipse (r);                                 // the hole
            g.setColour (lit ? Colours::accent : face.darker (0.2f));
            g.drawEllipse (r.expanded (2.5f), lit ? 2.0f : 1.0f);
            }

            // normalled: an arc over the jack (solid while it carries, faint once a cable breaks it)
            {
                const auto& marks = s.output ? srcNormalMark : dstNormalMark;
                const char mark = juce::isPositiveAndBelow (j.flat, (int) marks.size()) ? marks[(size_t) j.flat] : 0;
                const bool normalled = mark != 0, live = mark == 2;
                if (normalled)
                {
                    const float rr = r.getWidth() * 0.5f + (j.midi ? 6.0f : 5.0f);
                    juce::Path arc;
                    arc.addCentredArc (j.centre.x, j.centre.y, rr, rr, 0.0f, -0.9f, 0.9f, true);
                    g.setColour (ink.withAlpha (live ? 0.85f : 0.25f));
                    g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

            g.setColour (inkDim);
            g.setFont (font (9.5f));
            const auto labelArea = juce::Rectangle<float> (jackPitch + 6.0f, labelH).withCentre ({ j.centre.x, j.centre.y + jackPitch * 0.5f + labelH * 0.5f });
            g.drawText (j.label, labelArea, juce::Justification::centred, false);
            if (j.track >= 0)
                if (auto& tc = melville::tracks().colour; tc != nullptr)
                    if (const auto c = tc (j.track); ! c.isTransparent())
                    {
                        g.setColour (c);                // the track's colour under its number
                        g.fillRoundedRectangle (labelArea.withSizeKeepingCentre (jackPitch * 0.6f, 3.0f).translated (0.0f, labelH * 0.5f + 1.0f), 1.5f);
                    }
        }
    }
}

void PatchCanvas::drawCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour c, float thickness)
{
    // a cable hangs: the further apart the plugs, the deeper the sag
    const float sag = 30.0f + 0.22f * a.getDistanceFrom (b);
    juce::Path path;
    path.startNewSubPath (a);
    // straight up and down (columns line up) it would hide the jacks between: bow it aside
    const float bow = std::abs (a.x - b.x) < 60.0f ? 34.0f + 0.12f * std::abs (a.y - b.y) : 0.0f;
    path.cubicTo (a.translated (bow, sag), b.translated (bow, sag), b);
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
    const juce::Colour midiCable (0xff1e1e20);
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
        grp.colour = sj->midi ? midiCable : panels[(size_t) sp].colour;       // (MIDI: black 5-pin cable)
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
        const float bow = std::abs (a.x - b.x) < 60.0f ? 34.0f + 0.12f * std::abs (a.y - b.y) : 0.0f;
        const auto mid = (a + b) * 0.5f + juce::Point<float> (bow * 0.75f, sag * 0.75f);
        auto tag = juce::Rectangle<float> (34.0f, 18.0f).withCentre (mid);
        g.setColour (juce::Colour (0xff141414));
        g.fillRoundedRectangle (tag, 9.0f);
        g.setColour (grp.colour == midiCable ? juce::Colour (0xffb9b6ae) : grp.colour);
        g.drawRoundedRectangle (tag, 9.0f, 1.5f);
        g.setFont (font (11.0f, true));
        g.drawText (juce::String ((int) grp.ends.size()), tag, juce::Justification::centred);
        multicores.push_back ({ std::get<0> (key), std::get<1> (key), std::get<2> (key), std::get<3> (key), mid });
    }
}

void PatchCanvas::drawNormals (juce::Graphics& g) const
{
    // the wiring behind the bay, where it's carrying: thin dashed lines (a run of them as one)
    auto& pb = hub.getPatchbay();
    struct Group { std::vector<std::pair<juce::Point<float>, juce::Point<float>>> ends; juce::Colour colour; };
    std::map<std::tuple<int, int, int, int>, Group> groups;
    for (auto& [d, src] : pb.getAllNormals())
    {
        if (! juce::isPositiveAndBelow (d, (int) dstNormalMark.size()) || dstNormalMark[(size_t) d] != 2) continue;
        int sp = -1, dp = -1;
        const auto* ss = sectionFor (src, true, &sp);
        const auto* ds = sectionFor (d, false, &dp);
        const auto* sj = jackFor (src, true);
        const auto* dj = jackFor (d, false);
        if (ss == nullptr || ds == nullptr || sj == nullptr || dj == nullptr) continue;
        auto& grp = groups[{ sp, (int) (ss - panels[(size_t) sp].sections.data()), dp, (int) (ds - panels[(size_t) dp].sections.data()) }];
        grp.colour = sj->midi ? juce::Colour (0xffb9b6ae) : panels[(size_t) sp].colour;
        grp.ends.push_back ({ sj->centre, dj->centre });
    }

    auto dashed = [&g] (juce::Point<float> a, juce::Point<float> b, juce::Colour c, float thickness)
    {
        juce::Path path;
        path.startNewSubPath (a);
        const float bow = std::abs (a.x - b.x) < 60.0f ? 22.0f + 0.08f * std::abs (a.y - b.y) : 0.0f;
        const float sag = 12.0f + 0.08f * a.getDistanceFrom (b);
        path.cubicTo (a.translated (bow, sag), b.translated (bow, sag), b);
        juce::Path dash;
        const float lengths[] = { 6.0f, 4.0f };
        juce::PathStrokeType (thickness).createDashedStroke (dash, path, lengths, 2);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.strokePath (path, juce::PathStrokeType (thickness + 1.5f));
        g.setColour (c.withAlpha (0.9f));
        g.fillPath (dash);
    };

    for (auto& [key, grp] : groups)
    {
        if (showEveryCable || grp.ends.size() < 3)
        {
            for (auto& [a, b] : grp.ends) dashed (a, b, grp.colour, 1.5f);
            continue;
        }
        juce::Point<float> a, b;
        for (auto& [x, y] : grp.ends) { a += x; b += y; }
        dashed (a / (float) grp.ends.size(), b / (float) grp.ends.size(), grp.colour, 3.0f);
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
                const auto& j = s.jacks[(size_t) h.jack];
                const auto& p = panels[(size_t) h.panel];
                tip << p.title << "  " << s.title << " " << j.label << (s.output ? "  (output)" : "  (input)");
                if (j.track >= 0)
                    if (auto& tn = melville::tracks().name; tn != nullptr)
                        if (const auto n = tn (j.track); n.isNotEmpty()) tip << "  \"" << n << "\"";
                tip << "\n" << s.description;
                juce::StringArray to;
                for (auto& [d, c] : pb.getAllCords())
                {
                    if (s.output && c.source == j.flat) to.add (pb.destName (d));
                    if (! s.output && d == j.flat)      to.add (pb.sourceName (c.source));
                }
                tip << "\n" << (to.isEmpty() ? juce::String ("Nothing cabled.")
                                               : (s.output ? "Cabled to: " : "Cabled from: ") + to.joinIntoString (", "));
                if (s.output)
                {
                    juce::StringArray normalled;
                    for (auto& [d, src] : pb.getAllNormals())
                        if (src == j.flat) normalled.add (pb.destName (d) + (pb.isNormalLive (d) ? "" : " (broken)"));
                    if (! normalled.isEmpty()) tip << "\nNormalled to: " << normalled.joinIntoString (", ");
                }
                else if (const int ns = pb.normalOf (j.flat); ns >= 0)
                {
                    tip << "\nNormalled from " << pb.sourceName (ns) << (pb.isFullNormal (j.flat) ? " (full normal)" : " (half-normal)");
                    if (pb.isNormalLive (j.flat))    tip << ": carrying.";
                    else if (pb.isDestPlugged (j.flat)) tip << ": broken by the cable in this jack.";
                    else                             tip << ": broken by the cable in " << pb.sourceName (ns) << ".";
                }
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

    // an input's normal (the wiring behind the bay)
    if (! s.output)
    {
        m.addSeparator();
        if (const int ns = pb.normalOf (flat); ns >= 0)
        {
            m.addSectionHeader ("Normalled from " + pb.sourceName (ns));
            m.addItem (2, "Half-normal (a cable in " + pb.sourceName (ns) + " doesn't break it)", true, ! pb.isFullNormal (flat));
            m.addItem (3, "Full normal (a cable in either jack breaks it)", true, pb.isFullNormal (flat));
            m.addItem (4, "Take the normal out");
        }
        else
        {
            m.addItem (-1, "Not normalled", false);
        }
        if (cords.size() == 1 && pb.normalOf (flat) != cords.front().second.source)
            m.addItem (5, "Wire this cable behind the bay (make it the normal)");
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [this, cords, levels, flat] (int r)
                     {
                         auto& p = hub.getPatchbay();
                         if (r == 1) { for (auto& [d, c] : cords) p.disconnect (c.source, d); return; }
                         if (r == 2 || r == 3) { p.setNormal (p.normalOf (flat), flat, r == 3); return; }
                         if (r == 4) { p.setNormal (-1, flat); return; }
                         if (r == 5 && cords.size() == 1)
                         {
                             const int src = cords.front().second.source;
                             p.disconnect (src, flat);
                             p.setNormal (src, flat);
                             return;
                         }
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
    defaultButton.setTooltip ("Wire the bay the way MELVILLE starts (interface > Hawthorne > EMERSON > WHITMAN > back to Hawthorne) and pull every cable");
    defaultButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Default patch",
            "Rewire the normals the way MELVILLE starts and pull every cable?", "Rewire", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (ok) hub.defaultPatch(); }));
    };
    clearButton.setTooltip ("Pull every cable out of the patchbay. The normals stay, so everything goes back to its usual wiring");
    clearButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Pull every cable",
            "Pull every cable out? The normals stay, so the studio goes back to its usual wiring.", "Pull them", "Cancel", this,
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

    // AUDIO and MIDI: the same panels, in the same order, each with its own jacks
    for (auto* t : { &audioTab, &midiTab })
    {
        t->setRadioGroupId (0x6d69);
        t->setClickingTogglesState (true);
        t->setColour (juce::TextButton::buttonOnColourId, Colours::accent);
        t->setColour (juce::TextButton::textColourOnId, Colours::bg);
        t->setWantsKeyboardFocus (false);
        addAndMakeVisible (t);
    }
    audioTab.setConnectedEdges (juce::Button::ConnectedOnRight);
    midiTab.setConnectedEdges (juce::Button::ConnectedOnLeft);
    audioTab.setToggleState (true, juce::dontSendNotification);
    audioTab.setTooltip ("The audio patchbay");
    midiTab.setTooltip ("The MIDI patchbay: 5-pin DIN, one cable carries all sixteen channels");
    audioTab.onClick = [this] { if (audioTab.getToggleState()) { canvas.setMidiView (false); resized(); repaint(); } };
    midiTab.onClick  = [this] { if (midiTab.getToggleState())  { canvas.setMidiView (true);  resized(); repaint(); } };
    addAndMakeVisible (everyCable);
    normalsShown.setTooltip ("Draw the wiring behind the bay (the normals that are carrying) as dashed lines");
    normalsShown.setColour (juce::ToggleButton::textColourId, Colours::text);
    normalsShown.setColour (juce::ToggleButton::tickColourId, Colours::accent);
    normalsShown.setToggleState (true, juce::dontSendNotification);
    normalsShown.onClick = [this] { canvas.setShowNormals (normalsShown.getToggleState()); };
    addAndMakeVisible (normalsShown);

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
    g.drawText (midiTab.getToggleState()
                    ? "MIDI: OUT to IN; one cable carries all sixteen channels. A cable into a normalled IN breaks its normal."
                    : "Drag OUT to IN; drag a plug out to move it. A cable into a normalled IN breaks its normal.",
                bar.reduced (14, 0).withTrimmedLeft (170).withTrimmedRight (670), juce::Justification::centredLeft, true);
}

void PatchbayView::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (46).reduced (10, 9);
    audioTab.setBounds (bar.removeFromLeft (76));
    midiTab.setBounds (bar.removeFromLeft (76));
    for (auto* b : { &openButton, &saveButton, &clearButton, &defaultButton })
    {
        b->setBounds (bar.removeFromRight (b == &clearButton ? 96 : 80));
        bar.removeFromRight (6);
    }
    everyCable.setBounds (bar.removeFromRight (160));
    normalsShown.setBounds (bar.removeFromRight (140));
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
