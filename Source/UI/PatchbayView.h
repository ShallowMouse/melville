#pragma once

// The patchbay window: pick what you're patching FROM (a part's outputs) and TO (a
// part's inputs), and click the grid to make or break a cord. A source can feed any
// number of destinations; a destination with several cords hears them summed.
// Right-click a cord to set its level.

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/Hub.h"

namespace melville
{

class PatchGrid : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    explicit PatchGrid (Patchbay&);

    void setPorts (int fromPort, int toPort, int maxTracks);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int cell = 18, labelWidth = 220, headerHeight = 150;

private:
    std::pair<int, int> cellAt (juce::Point<int>) const;    // (column, row) or -1
    Patchbay& patchbay;
    std::vector<int> sources, dests;                          // flat numbers shown
    juce::String fromId, toId;
    int hoverCol = -1, hoverRow = -1;
};

class PatchbayView : public juce::Component
{
public:
    explicit PatchbayView (Hub&);
    ~PatchbayView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    void save();
    void open();

    Hub& hub;
    juce::Label fromLabel { {}, "FROM" }, toLabel { {}, "TO" };
    juce::ComboBox fromBox, toBox;
    juce::TextButton defaultButton { "DEFAULT" }, clearButton { "CLEAR VIEW" }, saveButton { "SAVE" }, openButton { "OPEN" };
    PatchGrid grid;
    juce::Viewport viewport;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace melville
