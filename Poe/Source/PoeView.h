#pragma once

// POE's window: the shared part header, then the sequencer as a piece of early-80s hardware
// (charcoal, white legends, red LEDs, keys in red, orange, yellow and cream). On
// the left the programmer (patterns, rate, direction, scale, the MIDI and CV settings); on the
// right the sixteen steps, each a column with its lamp, a readout of its note, the three
// knobs of rows A, B and C, and its key (PLAY / TIE / REST / SKIP).

#include <JuceHeader.h>
#include "PoeEngine.h"
#include "PequodLookAndFeel.h"
#include "Hardware.h"
#include "../../Shared/PartHeader.h"
#include "../../Shared/Eighties.h"

namespace poe
{

/** PEQUOD's look with POE's lamp cyan (for menus, tooltips and the like). */
class LookAndFeel : public PequodLookAndFeel
{
public:
    static juce::Colour cyan()   { return partui::accent ("poe"); }
    static juce::Colour cyanLo() { return cyan().withMultipliedBrightness (0.42f).withMultipliedSaturation (0.8f); }

    LookAndFeel()
    {
        for (auto id : { (int) juce::PopupMenu::highlightedBackgroundColourId, (int) juce::TextButton::buttonOnColourId,
                         (int) juce::AlertWindow::outlineColourId, (int) juce::TooltipWindow::outlineColourId })
            setColour (id, cyanLo());
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (Pequod::Colours::panel);
        g.setColour (cyanLo());
        g.drawRect (0, 0, w, h);
    }
};

/** A knob on the panel: a cream cap on a black skirt, over a printed scale (drag up and down;
    shift for fine; double-click for its usual setting; a wheel turns it too). `choices` > 1
    makes it a switch of that many positions. */
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<float()> get;                    // 0..1
    std::function<void (float)> set;
    std::function<juce::String (float)> text;      // the readout under it (none: no readout)
    juce::String label;                            // engraved over it (empty: none)
    int choices = 0;
    float normal = 0.0f;
    bool small = false;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    /** Its value again, if it changed elsewhere (a pattern, the session): true if it did. */
    bool refresh();

private:
    float shown = -1.0f, dragStart = 0.0f;
    void change (float v);
    juce::Rectangle<float> knobArea() const;
};

class View : public juce::Component, private juce::Timer
{
public:
    View();
    ~View() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    Rack& getRack() noexcept { return rack; }
    static juce::Colour accent() { return partui::accent ("poe"); }

private:
    class ModeSwitch;
    class PatternKey;

    void timerCallback() override;
    void refreshAll();
    void changed();
    juce::String statusLine() const;
    void showPatternMenu();

    juce::AudioDeviceManager deviceManager;
    Rack rack;
    Engine* cur = &rack.seq (0);                       // the sequencer on the panel
    int selected = 0;
    void select (int i);

    juce::Rectangle<int> headerArea, statusArea, programmerArea, stepsArea, patternArea;
    std::array<juce::Rectangle<int>, 6> rowArea;          // lamps, readouts, A, B, C, switches (across the steps)
    std::array<juce::Rectangle<int>, Engine::numSteps> columnArea, lampArea, readoutArea;

    partui::Key runKey { "RUN: play the sequence while MELVILLE's transport rolls", partui::Icon::power, accent() };
    partui::Key randomKey { "Random: new notes (and gates and ties) on this pattern", partui::Icon::none, accent() };
    partui::Key patternKey { "Pattern: copy, clear, random pitches", partui::Icon::list, accent() };

    juce::OwnedArray<PatternKey> patternKeys;
    juce::OwnedArray<partui::Key> seqKeys;                     // the sequencer keys, 1-4
    std::array<int, Rack::numSequencers> runningShown {};
    juce::OwnedArray<Knob> settings;                         // the programmer's knobs
    std::array<std::array<std::unique_ptr<Knob>, 3>, Engine::numSteps> stepKnobs;
    juce::OwnedArray<ModeSwitch> modes;

    int litShown = -2, transposeShown = 0, noteShown = -2;
    std::array<int, Engine::numSteps> notesShown {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
};

} // namespace poe
