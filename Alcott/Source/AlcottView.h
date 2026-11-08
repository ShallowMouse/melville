#pragma once

// ALCOTT's window: the shared part header, the eight slots down the left, and the picked
// slot as a 19-inch rack on the right (its instrument on top, then its effects).

#include <JuceHeader.h>
#include "AlcottEngine.h"
#include "PequodLookAndFeel.h"
#include "../../Shared/PartHeader.h"

namespace alcott
{

class View : public juce::Component, private juce::Timer
{
public:
    View();
    ~View() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    Engine& getEngine() noexcept { return engine; }

    static juce::Colour accent() { return partui::accent ("alcott"); }

private:
    class SlotList;
    class Unit;
    class Blank;

    void timerCallback() override;
    void select (int slot);
    void rebuildRack();
    void layoutRack();
    void refreshControls();
    void showInstrumentMenu();
    void showEffectMenu (int insertAt);
    void showMidiInputs();
    void changed();
    juce::String statusLine() const;

    juce::AudioDeviceManager deviceManager;
    Engine engine;
    int current = 0;

    juce::Rectangle<int> headerArea, statusArea, controlsArea;
    partui::Key midiKey { "MIDI inputs: which keyboards play ALCOTT", partui::Icon::midi, accent() };

    std::unique_ptr<SlotList> list;

    juce::ComboBox source, channel;
    juce::Slider level;
    juce::TextButton mute { "MUTE" }, clear { "CLEAR" };

    juce::Viewport rackView;
    juce::Component rack;
    juce::OwnedArray<Unit> units;
    std::unique_ptr<Blank> chooseInstrument, addEffect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
};

} // namespace alcott
