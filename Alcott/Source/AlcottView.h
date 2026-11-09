#pragma once

// ALCOTT's window: the shared part header, the eight slots down the left, and the picked
// slot as a 19-inch rack on the right (its instrument on top, then its effects).

#include <JuceHeader.h>
#include "AlcottEngine.h"
#include "PequodLookAndFeel.h"
#include "../../Shared/PartHeader.h"

namespace alcott
{

/** PEQUOD's look (as IRVING has it) with ALCOTT's plum wherever IRVING has its oxblood. */
class LookAndFeel : public PequodLookAndFeel
{
public:
    static juce::Colour plum()   { return partui::accent ("alcott"); }
    static juce::Colour plumLo() { return plum().withMultipliedBrightness (0.45f).withMultipliedSaturation (0.8f); }

    LookAndFeel()
    {
        auto scheme = getCurrentColourScheme();
        scheme.setUIColour (ColourScheme::defaultFill, plumLo());
        scheme.setUIColour (ColourScheme::highlightedFill, plum());
        setColourScheme (scheme);
        PequodLookAndFeel::setColour (juce::TextButton::buttonOnColourId, plum());
        for (auto id : { (int) juce::Slider::rotarySliderFillColourId, (int) juce::Slider::thumbColourId, (int) juce::TextEditor::focusedOutlineColourId,
                         (int) juce::CaretComponent::caretColourId, (int) juce::ComboBox::arrowColourId, (int) juce::PopupMenu::highlightedBackgroundColourId,
                         (int) juce::ToggleButton::tickColourId, (int) juce::AlertWindow::outlineColourId })
            setColour (id, plum());
        for (auto id : { (int) juce::Slider::trackColourId, (int) juce::TextEditor::highlightColourId, (int) juce::ScrollBar::thumbColourId,
                         (int) juce::TooltipWindow::outlineColourId })
            setColour (id, plumLo());
        // (PEQUOD's own colours again, which setColourScheme above put back to defaults)
        PequodLookAndFeel fresh;
        for (auto id : { (int) juce::TextButton::buttonColourId, (int) juce::TextButton::textColourOffId, (int) juce::TextButton::textColourOnId,
                         (int) juce::ComboBox::backgroundColourId, (int) juce::ComboBox::outlineColourId, (int) juce::ComboBox::textColourId,
                         (int) juce::PopupMenu::backgroundColourId, (int) juce::PopupMenu::textColourId, (int) juce::PopupMenu::highlightedTextColourId,
                         (int) juce::Label::textColourId, (int) juce::Slider::backgroundColourId, (int) juce::Slider::textBoxTextColourId,
                         (int) juce::Slider::textBoxOutlineColourId, (int) juce::Slider::rotarySliderOutlineColourId,
                         (int) juce::TextEditor::backgroundColourId, (int) juce::TextEditor::outlineColourId, (int) juce::TextEditor::textColourId,
                         (int) juce::ListBox::backgroundColourId, (int) juce::ResizableWindow::backgroundColourId, (int) juce::AlertWindow::backgroundColourId,
                         (int) juce::AlertWindow::textColourId, (int) juce::TooltipWindow::backgroundColourId, (int) juce::TooltipWindow::textColourId })
            setColour (id, fresh.findColour (id));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
            return PequodLookAndFeel::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
        g.setColour (Pequod::Colours::well);
        g.fillRoundedRectangle (track, 2.0f);
        g.setColour (Pequod::Colours::line);
        g.drawLine (r.getCentreX(), track.getY() - 4, r.getCentreX(), track.getBottom() + 4, 1.0f);
        auto thumb = juce::Rectangle<float> (10.0f, juce::jmin (18.0f, r.getHeight())).withCentre ({ pos, r.getCentreY() });
        g.setColour (plum());
        g.fillRoundedRectangle (thumb, 2.0f);
        g.setColour (Pequod::Colours::bg);
        g.drawLine (thumb.getCentreX(), thumb.getY() + 3, thumb.getCentreX(), thumb.getBottom() - 3, 1.0f);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (Pequod::Colours::panel);
        g.setColour (plumLo());
        g.drawRect (0, 0, w, h);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool) override
    {
        auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
        g.setColour (over ? plum() : plumLo());
        g.fillRoundedRectangle (thumb.reduced (3).toFloat(), 2.0f);
    }
};

/** A rack unit's power light, in plum. */
class PowerLight : public juce::Button
{
public:
    PowerLight() : Button ("power") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
        if (getToggleState())
        {
            g.setColour (LookAndFeel::plum().withAlpha (0.25f));
            g.fillEllipse (r.expanded (3.0f));
            g.setColour (LookAndFeel::plum());
        }
        else
            g.setColour (over ? juce::Colour (0xff4a4a4a) : juce::Colour (0xff333333));
        g.fillEllipse (r);
        g.setColour (Pequod::Colours::rail);
        g.drawEllipse (r, 1.5f);
    }
};

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
    partui::Key midiKey { "Settings: how long THOREAU's steps hold (MIDI is cabled on MELVILLE's PATCHBAY)", partui::Icon::settings, accent() };

    std::unique_ptr<SlotList> list;

    juce::ComboBox channel;
    juce::Slider level;
    juce::TextButton mute { "MUTE" }, clear { "CLEAR" };

    juce::Viewport rackView;
    juce::Component rack;
    juce::OwnedArray<Unit> units;
    std::unique_ptr<Blank> chooseInstrument, addEffect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
};

} // namespace alcott
