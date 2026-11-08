// Hawthorne inside MELVILLE (compiled with Hawthorne's settings).
#include "Parts.h"
#include "UI/MainComponent.h"
#include "UI/NetworkWindow.h"
#include "../UI/WindowFrame.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makeHawthorne()
{
    // Hawthorne's own settings file, so MELVILLE's Hawthorne has the same machine, reels and library
    auto props = std::make_shared<juce::ApplicationProperties>();
    juce::PropertiesFile::Options o;
    o.applicationName     = "Hawthorne";
    o.filenameSuffix      = ".settings";
    o.folderName          = "Hawthorne";
    o.osxLibrarySubFolder = "Application Support";
    props->setStorageParameters (o);

    auto ui = std::make_unique<PartUI>();
    ui->id = "hawthorne";
    ui->title = "Hawthorne";
    ui->background = hawthorne::Colours::bg;
    ui->minWidth = 1340;
    ui->minHeight = 600;
    ui->nativeTitleBar = false;          // (MELVILLE's bar on every window; Hawthorne's own window keys are hidden)
    ui->fullScreen = true;
    auto* mc = new hawthorne::MainComponent (*props);
    ui->content.reset (mc);
    ui->shutdown = [mc, props] { mc->shutdown(); props->saveIfNeeded(); };
    ui->keep = props;
    return ui;
}

namespace
{
    // Hawthorne Link's window, as MELVILLE's NETWORK window (with Hawthorne's look for its controls)
    struct MelvilleNetworkWindow : hawthorne::NetworkWindow
    {
        explicit MelvilleNetworkWindow (hawthorne::NetReceiver& rx) : hawthorne::NetworkWindow (rx)
        {
            setName ("MELVILLE \xc2\xb7 MELVILLE LINK");
            setLookAndFeel (&lnf);
            // MELVILLE's bar and colour (it's MELVILLE's link, not Hawthorne's)
            getPanel().setAccent (melville::Colours::accent, melville::Colours::accentLo);
            for (auto id : { (int) juce::Slider::thumbColourId, (int) juce::Slider::trackColourId, (int) juce::TextButton::buttonOnColourId,
                             (int) juce::Label::outlineWhenEditingColourId, (int) juce::TextEditor::focusedOutlineColourId, (int) juce::CaretComponent::caretColourId })
                lnf.setColour (id, melville::Colours::accent);
            clearContentComponent();
            frameWindow (*this, &getPanel(), false, "MELVILLE Link", melville::Colours::accent,
                         juce::ImageCache::getFromMemory (BinaryData::MELVILLE_png, BinaryData::MELVILLE_pngSize).rescaled (64, 64, juce::Graphics::highResamplingQuality),
                         WindowTitleBar::minimise | WindowTitleBar::close);
            setResizable (true, false);
            setResizeLimits (700, 420 + WindowTitleBar::height, 2000, 1400);
            centreWithSize (900, 560 + WindowTitleBar::height);
        }
        ~MelvilleNetworkWindow() override { setLookAndFeel (nullptr); }
        hawthorne::HawthorneLookAndFeel lnf;
    };
}

std::unique_ptr<juce::DocumentWindow> makeNetworkWindow (hawthorne::NetReceiver& rx, std::function<void()> onChanged)
{
    auto w = std::make_unique<MelvilleNetworkWindow> (rx);
    w->getPanel().onChanged = std::move (onChanged);
    return w;
}

} // namespace melville::parts
