#pragma once

// Making each app's main screen inside MELVILLE. Each maker lives in its own .cpp,
// compiled with that app's settings (see cmake/Parts.cmake), so the apps' headers
// never meet in one file.

#include <juce_gui_basics/juce_gui_basics.h>

namespace hawthorne { class NetReceiver; }

namespace melville
{

struct PartUI
{
    juce::String id;                                  // "hawthorne", "emerson" ...
    juce::String title;                               // window title
    juce::Colour background;
    int minWidth = 800, minHeight = 600;
    bool nativeTitleBar = true;                       // Hawthorne draws its own
    bool fullScreen = false;                          // Hawthorne and PEQUOD Host open maximised

    // destroyed in this order: content, then its look-and-feel, then anything it kept
    std::unique_ptr<juce::Component> content;
    std::unique_ptr<juce::LookAndFeel> lookAndFeel;
    std::shared_ptr<void> keep;                       // e.g. Hawthorne's settings files
    std::function<void()> shutdown;                   // before anything goes (Hawthorne finishes a take)

    ~PartUI()
    {
        content.reset();
        lookAndFeel.reset();
        keep.reset();
    }
};

namespace parts
{
    std::unique_ptr<PartUI> makeHawthorne();
    std::unique_ptr<PartUI> makeEmerson();
    std::unique_ptr<PartUI> makeWhitman();
    std::unique_ptr<PartUI> makeThoreau();
    std::unique_ptr<PartUI> makePequod();

    /** Hawthorne Link's window, for MELVILLE's NETWORK (onChanged: save the settings). */
    std::unique_ptr<juce::DocumentWindow> makeNetworkWindow (hawthorne::NetReceiver&, std::function<void()> onChanged);
}

} // namespace melville
