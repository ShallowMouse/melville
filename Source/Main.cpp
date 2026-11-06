// MELVILLE: Hawthorne, EMERSON, WHITMAN, THOREAU and PEQUOD Host as one program.
//
// MELVILLE owns the audio interface and runs every part from its own audio callback,
// with a patchbay between them (Core/Hub.h). Each part keeps its own window and look;
// the MELVILLE bar opens them, the patchbay and the interface settings. Closing a
// part's window only hides it: the part keeps running. Closing the bar quits.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/Hub.h"
#include "Parts/Parts.h"
#include "UI/MelvilleLookAndFeel.h"
#include "UI/PatchbayView.h"

namespace melville
{

//==============================================================================
/** A part's own window. */
class PartWindow : public juce::DocumentWindow
{
public:
    explicit PartWindow (PartUI& p)
        : DocumentWindow ("MELVILLE \xc2\xb7 " + p.title, p.background, DocumentWindow::allButtons), part (p)
    {
        if (part.lookAndFeel != nullptr) setLookAndFeel (part.lookAndFeel.get());
        setUsingNativeTitleBar (part.nativeTitleBar);
        if (! part.nativeTitleBar) setTitleBarHeight (0);
        setContentNonOwned (part.content.get(), true);
        setResizable (true, true);
        setResizeLimits (part.minWidth, part.minHeight, 10000, 10000);
        centreWithSize (juce::jmax (getWidth(), part.minWidth), juce::jmax (getHeight(), part.minHeight));
    }

    ~PartWindow() override
    {
        clearContentComponent();
        setLookAndFeel (nullptr);
    }

    void show()
    {
        if (! isVisible())
        {
            setVisible (true);
            if (part.fullScreen && ! opened) setFullScreen (true);
            opened = true;
        }
        if (isMinimised()) setMinimised (false);
        toFront (true);
    }

    void closeButtonPressed() override { setVisible (false); }   // the part keeps running

private:
    PartUI& part;
    bool opened = false;
};

//==============================================================================
/** The MELVILLE bar: a button per part, the patchbay, the interface. */
class Bar : public juce::Component, private juce::Timer
{
public:
    Bar (Hub& h, std::vector<std::unique_ptr<PartUI>>& p, std::vector<std::unique_ptr<PartWindow>>& w)
        : hub (h), parts (p), windows (w)
    {
        for (size_t i = 0; i < parts.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (parts[i]->title.toUpperCase()));
            b->setColour (juce::TextButton::textColourOffId, Colours::forPart (parts[i]->id));
            b->onClick = [this, i] { windows[i]->show(); };
            addAndMakeVisible (b);
        }
        patchButton.onClick = [this] { showPatchbay(); };
        audioButton.onClick = [this] { showAudio(); };
        addAndMakeVisible (patchButton);
        addAndMakeVisible (audioButton);
        startTimerHz (4);
        setSize (1180, 64);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::panel);
        auto r = getLocalBounds().reduced (14, 0);
        g.setColour (Colours::accent);
        g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        g.drawText ("MELVILLE", r.removeFromLeft (130), juce::Justification::centredLeft);

        // a lamp under each part: lit while it's running in the hub
        for (int i = 0; i < buttons.size(); ++i)
        {
            auto b = buttons[i]->getBounds();
            const bool on = hub.isPartConnected (parts[(size_t) i]->id);
            g.setColour (on ? Colours::forPart (parts[(size_t) i]->id) : Colours::lampOff);
            g.fillEllipse ((float) b.getCentreX() - 3.0f, (float) b.getBottom() + 4.0f, 6.0f, 6.0f);
        }

        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.setColour (Colours::textDim);
        auto* dev = hub.deviceManager.getCurrentAudioDevice();
        const juce::String status = dev == nullptr ? juce::String ("NO INTERFACE")
            : dev->getName().toUpperCase() + "  " + juce::String (dev->getCurrentSampleRate() / 1000.0, 1) + " kHz  "
              + juce::String (dev->getCurrentBufferSizeSamples()) + "  CPU " + juce::String (juce::roundToInt (hub.getLoad() * 100.0f)) + "%";
        g.drawText (status, statusArea, juce::Justification::centredRight, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12).withTrimmedBottom (6);
        r.removeFromLeft (130);
        statusArea = r.removeFromRight (260);
        for (auto* b : { &audioButton, &patchButton }) { b->setBounds (r.removeFromRight (90)); r.removeFromRight (6); }
        r.removeFromRight (10);
        const int w = buttons.isEmpty() ? 0 : juce::jmin (130, r.getWidth() / buttons.size() - 6);
        for (auto* b : buttons) { b->setBounds (r.removeFromLeft (w)); r.removeFromLeft (6); }
    }

    void showPatchbay()
    {
        if (patchWindow == nullptr)
        {
            patchWindow = std::make_unique<juce::DocumentWindow> ("MELVILLE \xc2\xb7 PATCHBAY", Colours::bg, juce::DocumentWindow::allButtons);
            patchWindow->setUsingNativeTitleBar (true);
            patchWindow->setContentOwned (new PatchbayView (hub), true);
            patchWindow->setResizable (true, false);
            patchWindow->centreWithSize (patchWindow->getWidth(), patchWindow->getHeight());
        }
        patchWindow->setVisible (true);
        patchWindow->toFront (true);
    }

    void showAudio()
    {
        auto* sel = new juce::AudioDeviceSelectorComponent (hub.deviceManager, 0, 64, 0, 64, true, false, false, false);
        sel->setSize (560, 460);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "MELVILLE \xc2\xb7 INTERFACE";
        o.dialogBackgroundColour = Colours::bg;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    void closePatchbay() { patchWindow = nullptr; }
    juce::Component* getPatchbayContent() { return patchWindow != nullptr ? patchWindow->getContentComponent() : nullptr; }

private:
    void timerCallback() override { repaint(); }

    Hub& hub;
    std::vector<std::unique_ptr<PartUI>>& parts;
    std::vector<std::unique_ptr<PartWindow>>& windows;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::TextButton patchButton { "PATCHBAY" }, audioButton { "INTERFACE" };
    juce::Rectangle<int> statusArea;
    std::unique_ptr<juce::DocumentWindow> patchWindow;
};

class BarWindow : public juce::DocumentWindow
{
public:
    explicit BarWindow (Bar* bar) : DocumentWindow ("MELVILLE", Colours::panel, DocumentWindow::minimiseButton | DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (bar, true);
        setResizable (true, false);
        setResizeLimits (1000, 64, 4000, 64);
        setTopLeftPosition (40, 40);
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

} // namespace melville

//==============================================================================
class MelvilleApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MELVILLE"; }
    const juce::String getApplicationVersion() override { return MELVILLE_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        using namespace melville;
        juce::PropertiesFile::Options o;
        o.applicationName = "MELVILLE";
        o.filenameSuffix = ".settings";
        o.folderName = "MELVILLE";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        // the hub first: the parts open their devices on it as they're made
        hub = std::make_unique<Hub>();
        auto audio = props.getUserSettings()->getXmlValue ("audio");
        hub->initialiseAudio (audio.get());
        if (auto patch = props.getUserSettings()->getXmlValue ("patch"))
            hub->getPatchbay().fromXml (*patch);

        // every part, running from the start (windows open from the bar)
        parts.push_back (parts::makeHawthorne());
        parts.push_back (parts::makeEmerson());
        parts.push_back (parts::makeWhitman());
        parts.push_back (parts::makeThoreau());
        parts.push_back (parts::makePequod());
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);   // (Hawthorne sets its own as the default)

        for (auto& p : parts) windows.push_back (std::make_unique<PartWindow> (*p));
        bar = std::make_unique<BarWindow> (new Bar (*hub, parts, windows));
        windows.front()->show();   // Hawthorne, as you'd start the day

        // Test hook (the workflow's smoke test): report which parts came up, then quit
        const auto quitAfter = juce::SystemStats::getEnvironmentVariable ("MELVILLE_QUIT_AFTER_MS", {}).getIntValue();
        if (quitAfter > 0)
            juce::Timer::callAfterDelay (quitAfter, [this]
            {
                // pictures of the bar and the patchbay, for checking the look
                const auto snaps = juce::SystemStats::getEnvironmentVariable ("MELVILLE_SNAPSHOT_DIR", {});
                if (snaps.isNotEmpty())
                {
                    auto save = [&] (juce::Component* c, const char* name)
                    {
                        if (c == nullptr) return;
                        juce::FileOutputStream out (juce::File (snaps).getChildFile (name));
                        out.setPosition (0); out.truncate();
                        juce::PNGImageFormat().writeImageToStream (c->createComponentSnapshot (c->getLocalBounds()), out);
                    };
                    auto* b = dynamic_cast<melville::Bar*> (bar->getContentComponent());
                    save (b, "bar.png");
                    b->showPatchbay();
                    save (b->getPatchbayContent(), "patchbay.png");
                    for (size_t i = 1; i < windows.size(); ++i)
                    {
                        windows[i]->show();
                        save (parts[i]->content.get(), (parts[i]->id + ".png").toRawUTF8());
                    }
                }

                int up = 0;
                for (auto& p : parts)
                {
                    const bool on = hub->isPartConnected (p->id);
                    up += on ? 1 : 0;
                    std::printf ("MELVILLE smoke: %-10s %s\n", p->id.toRawUTF8(), on ? "running" : "NOT RUNNING");
                }
                std::printf ("MELVILLE smoke: %d of %d parts running, %d cords\n", up, (int) parts.size(),
                             (int) hub->getPatchbay().getAllCords().size());
                std::fflush (stdout);
                setApplicationReturnValue (up == (int) parts.size() ? 0 : 1);
                systemRequestedQuit();
            });
    }

    void shutdown() override
    {
        if (hub != nullptr)
        {
            props.getUserSettings()->setValue ("patch", hub->getPatchbay().toXml().get());
            if (auto xml = hub->deviceManager.createStateXml())
                props.getUserSettings()->setValue ("audio", xml.get());
        }
        for (auto& p : parts) if (p->shutdown) p->shutdown();   // Hawthorne finishes any take first

        // dialogs a part left open (PEQUOD Host's plug-in manager, say) go before the part does
        for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;)
            if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
            {
                dw->exitModalState (0);
                delete dw;
            }
        juce::ModalComponentManager::getInstance()->cancelAllModalComponents();

        bar = nullptr;
        windows.clear();
        while (! parts.empty()) parts.pop_back();               // the parts close their devices
        hub = nullptr;
        props.closeFiles();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (bar != nullptr) bar->toFront (true); }

private:
    juce::ApplicationProperties props;
    melville::MelvilleLookAndFeel lookAndFeel;
    std::unique_ptr<melville::Hub> hub;
    std::vector<std::unique_ptr<melville::PartUI>> parts;
    std::vector<std::unique_ptr<melville::PartWindow>> windows;
    std::unique_ptr<melville::BarWindow> bar;
};

START_JUCE_APPLICATION (MelvilleApplication)
