#pragma once

// MELVILLE's patchbay, laid out like a hardware one: a patch panel for each app, in the
// app's own colour, with its outputs on the top row of jacks and its inputs on the bottom
// row. Cables hang between the jacks.
//
//   drag from an OUTPUT jack to an INPUT jack       patch a cable
//   drag the plug out of an INPUT jack               move it to another input, or pull it out
//   drag a section's name onto another section's     patch the whole section, 1 to 1, 2 to 2 ...
//   right-click a jack or a multicore                levels, pull cables, normals
//
// NORMALS: the usual wiring is behind the bay, as on a hardware TT bay. An input is
// normalled from an output; a cable into the input breaks the normal (a FULL normal
// breaks with a cable in the output too). The arc over a jack says it's normalled: solid
// while the normal is carrying, faint once a cable has broken it. SHOW NORMALS draws them.
//
// A run of cables between the same two sections (24 tape tracks into EMERSON, say) is
// drawn as one multicore with its count; SHOW EVERY CABLE draws them all.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/Hub.h"

namespace melville
{

class PatchCanvas : public juce::Component,
                    public juce::SettableTooltipClient,
                    private juce::Timer
{
public:
    explicit PatchCanvas (Hub&);
    ~PatchCanvas() override;

    void rebuild();                  // the panels (when the reel or interface changes)
    void setShowEveryCable (bool);
    void setMidiView (bool);            // the MIDI tab: the MIDI jacks (else the audio ones)
    void setShowNormals (bool);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    int getContentHeight() const noexcept { return contentHeight; }
    std::function<void()> onShowEveryCable;    // the multicore menu turned it on

private:
    struct Jack
    {
        int flat = -1;                      // source or destination number in the patchbay
        juce::String label;                 // "7", "L", "LTC"
        int track = -1;                     // a tape track's jack: which (its name and colour come from the reel)
        bool midi = false;                  // a MIDI jack (5-pin DIN; MIDI cables only)
        juce::Point<float> centre;
    };

    struct Section
    {
        juce::String title;                 // "TRACKS", "MASTER", "TO DESK"
        bool output = true;                 // top row (sources) or bottom row (destinations)
        int column = 0;                     // outputs sit over the inputs they pair with
        juce::String description;           // what it's for (its jacks' tooltips)
        std::vector<Jack> jacks;
        juce::Rectangle<float> titleArea, area;
    };

    struct Panel
    {
        juce::String id, title;
        juce::Colour colour;
        std::vector<Section> sections;
        juce::Rectangle<float> area;
        juce::String note;                  // shown when it has no jacks
        int columns = 0;
    };

    struct Hit { int panel = -1, section = -1, jack = -1; bool titleOnly = false; };

    void layOut();
    Hit hitTest (juce::Point<float>) const;
    const Jack* jackFor (int flat, bool output) const;
    const Section* sectionFor (int flat, bool output, int* panelIndex = nullptr) const;
    juce::Colour colourOfSource (int flat) const;
    void drawPanel (juce::Graphics&, const Panel&) const;
    void drawCables (juce::Graphics&) const;
    void drawNormals (juce::Graphics&) const;
    static void drawCable (juce::Graphics&, juce::Point<float> a, juce::Point<float> b, juce::Colour, float thickness);
    void showJackMenu (const Hit&);
    void showMulticoreMenu (int srcPanel, int srcSection, int dstPanel, int dstSection);
    void timerCallback() override;

    Hub& hub;
    std::vector<Panel> panels;
    bool showEveryCable = false, midiView = false, showNormals = true;
    int contentHeight = 600;
    int lastTracks = -1, lastIns = -1, lastOuts = -1, tracksRev = -1;
    juce::String lastNetwork;
    juce::String networkKey() const;

    // dragging a cable
    struct Drag
    {
        bool active = false;
        bool fromTitle = false;             // a whole section
        Hit from;
        std::vector<int> sources;           // what the plug carries
        juce::Point<float> start, end;
    } drag;
    Hit hover;

    // multicores drawn this time (for clicking them)
    struct Multicore { int sp, ss, dp, ds; juce::Point<float> label; };
    mutable std::vector<Multicore> multicores;
    std::vector<char> srcNormalMark, dstNormalMark;   // (worked out in paint)
};

class PatchbayView : public juce::Component
{
public:
    explicit PatchbayView (Hub&);
    ~PatchbayView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void save();
    void open();

    Hub& hub;
    juce::TextButton normalsButton { "MAKE NORMALS" };
    juce::TextButton defaultButton { "DEFAULT" }, clearButton { "PULL ALL" }, saveButton { "SAVE" }, openButton { "OPEN" };
    juce::ToggleButton everyCable { "SHOW EVERY CABLE" }, normalsShown { "SHOW NORMALS" };
    juce::TextButton audioTab { "AUDIO" }, midiTab { "MIDI" };
    PatchCanvas canvas;
    juce::Viewport viewport;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace melville
