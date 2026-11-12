// MELVILLE: the parts' own small windows (their settings), in MELVILLE's frame. The app only:
// the tests don't build windows.
#include "MelvilleHosted.h"
#include "../UI/WindowFrame.h"
#include "BinaryData.h"

namespace melville
{

namespace
{
    /** A part's own small window: MELVILLE's frame, closes away. */
    struct PanelWindow : juce::DocumentWindow
    {
        PanelWindow (const juce::String& t, juce::Colour bg, juce::Colour accent) : DocumentWindow (t, bg, DocumentWindow::closeButton)
        {
            // MELVILLE's look, in the part's colour
            for (auto id : { (int) juce::Slider::thumbColourId, (int) juce::Slider::rotarySliderFillColourId, (int) juce::ComboBox::arrowColourId,
                             (int) juce::ToggleButton::tickColourId, (int) juce::TextButton::buttonOnColourId, (int) juce::TextEditor::focusedOutlineColourId,
                             (int) juce::PopupMenu::highlightedBackgroundColourId })
                look.setColour (id, accent);
            look.setColour (juce::Slider::trackColourId, accent.withMultipliedBrightness (0.5f));
            setLookAndFeel (&look);
        }
        ~PanelWindow() override { clearContentComponent(); setLookAndFeel (nullptr); }
        void closeButtonPressed() override { setVisible (false); }
        MelvilleLookAndFeel look;
    };
    /** The part's own mark for the window (its icon on the bar and the taskbar), MELVILLE's if it has none.
        Studio-wide windows (the new-session wizard) pass "melville" for MELVILLE's mark and colour. */
    juce::Image partIcon (const juce::String& id)
    {
        const void* data = BinaryData::MELVILLE_png; int size = BinaryData::MELVILLE_pngSize;
        if (id == "hawthorne") { data = BinaryData::HAWTHORNE_png; size = BinaryData::HAWTHORNE_pngSize; }
        if (id == "emerson")   { data = BinaryData::EMERSON_png;   size = BinaryData::EMERSON_pngSize; }
        if (id == "whitman")   { data = BinaryData::WHITMAN_png;   size = BinaryData::WHITMAN_pngSize; }
        if (id == "thoreau")   { data = BinaryData::THOREAU_png;   size = BinaryData::THOREAU_pngSize; }
        if (id.startsWith ("pequod") || id == "irving") { data = BinaryData::IRVING_png; size = BinaryData::IRVING_pngSize; }
        auto img = juce::ImageCache::getFromMemory (data, size);
        return img.isValid() ? img.rescaled (64, 64, juce::Graphics::highResamplingQuality) : img;
    }

    juce::OwnedArray<PanelWindow>& panels() { static juce::OwnedArray<PanelWindow> p; return p; }
}

void closePanels() { panels().clear(); }

void showPanel (std::unique_ptr<juce::Component> content, const juce::String& title, const juce::String& partId)
{
    for (auto* w : panels())
        if (w->getName() == title)
        {
            if (! w->isVisible())
            {
                // (asked again: its content fresh)
                w->clearContentComponent();
                frameWindow (*w, content.release(), true, title, Colours::forPart (partId), partIcon (partId), WindowTitleBar::close);
            }
            w->setVisible (true);
            w->toFront (true);
            return;
        }
    auto* w = panels().add (new PanelWindow (title, Colours::panel, Colours::forPart (partId)));
    frameWindow (*w, content.release(), true, title, Colours::forPart (partId), partIcon (partId), WindowTitleBar::close);
    w->setAlwaysOnTop (false);
    w->centreWithSize (w->getWidth(), w->getHeight());
    w->setVisible (true);
    w->toFront (true);
}


} // namespace melville
