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
#include "UI/Taskbar.h"
#include "Core/NetworkNode.h"

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

    // closing a part sends it down to its taskbar button (it keeps running and keeps its place)
    void closeButtonPressed() override { setMinimised (true); }

private:
    PartUI& part;
    bool opened = false;
};

//==============================================================================
/** One tile on the MELVILLE bar: a part's own icon (or a drawn one), its name, and a lamp. */
class Tile : public juce::Button
{
public:
    Tile (const juce::String& name, juce::Image icon, juce::Colour colour)
        : juce::Button (name), image (std::move (icon)), accent (colour)
    {
        setTooltip (name);
    }

    std::function<void (juce::Graphics&, juce::Rectangle<float>)> drawIcon;   // when there's no image
    std::function<bool()> isLit;                                               // the lamp (none if empty)

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        if (over || down)
        {
            g.setColour (Colours::panelHi.brighter (down ? 0.08f : 0.0f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        auto iconArea = r.removeFromTop (r.getHeight() - 26.0f).reduced (6.0f);
        const float side = juce::jmin (iconArea.getWidth(), iconArea.getHeight());
        iconArea = iconArea.withSizeKeepingCentre (side, side);
        if (image.isValid())
            g.drawImage (image, iconArea, juce::RectanglePlacement::centred);
        else if (drawIcon)
            drawIcon (g, iconArea);

        g.setColour (over ? accent.brighter (0.2f) : accent);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawFittedText (getName().toUpperCase(), r.removeFromTop (14.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        if (isLit)
        {
            const bool on = isLit();
            auto lamp = r.removeFromTop (10.0f).withSizeKeepingCentre (6.0f, 6.0f);
            g.setColour (on ? accent : Colours::lampOff);
            g.fillEllipse (lamp);
        }
    }

private:
    juce::Image image;
    juce::Colour accent;
};

static juce::Image loadIcon (const juce::String& id, int px = 128)
{
    const void* data = nullptr; int size = 0;
    if (id == "hawthorne") { data = BinaryData::hawthornelogo_png; size = BinaryData::hawthornelogo_pngSize; }
    if (id == "emerson")   { data = BinaryData::EMERSON_png;       size = BinaryData::EMERSON_pngSize; }
    if (id == "whitman")   { data = BinaryData::WHITMAN_png;       size = BinaryData::WHITMAN_pngSize; }
    if (id == "thoreau")   { data = BinaryData::THOREAU_png;       size = BinaryData::THOREAU_pngSize; }
    if (id == "pequod")    { data = BinaryData::PEQUODHost_png;    size = BinaryData::PEQUODHost_pngSize; }
    if (data == nullptr) return {};
    // scaled down once, so the bar draws it crisply and cheaply
    auto big = juce::ImageCache::getFromMemory (data, size);
    return big.isValid() ? big.rescaled (px, px, juce::Graphics::highResamplingQuality) : big;
}

//==============================================================================
/** The MELVILLE bar: every part's icon (click to bring up its window, lamp lit while it
    runs), then the patchbay and the interface. It stays on top of the parts' windows. */
class Bar : public juce::Component, private juce::Timer
{
public:
    Bar (Hub& h, std::vector<std::unique_ptr<PartUI>>& p, std::vector<std::unique_ptr<PartWindow>>& w)
        : hub (h), parts (p), windows (w)
    {
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto id = parts[i]->id;
            auto* t = tiles.add (new Tile (parts[i]->title, loadIcon (id), Colours::forPart (id)));
            t->isLit = [this, id] { return hub.isPartConnected (id); };
            t->onClick = [this, i] { windows[i]->show(); };
            addAndMakeVisible (t);
        }

        patchTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // a jack field with two cords
            g.setColour (Colours::panelHi);
            g.fillRoundedRectangle (r, r.getWidth() * 0.12f);
            const float step = r.getWidth() / 5.0f;
            g.setColour (Colours::textDim);
            for (int row = 0; row < 2; ++row)
                for (int c = 0; c < 4; ++c)
                    g.fillEllipse (r.getX() + step * (c + 1) - 3.5f, r.getY() + r.getHeight() * (row == 0 ? 0.3f : 0.7f) - 3.5f, 7.0f, 7.0f);
            g.setColour (Colours::accent);
            juce::Path cord;
            cord.startNewSubPath (r.getX() + step, r.getY() + r.getHeight() * 0.3f);
            cord.cubicTo (r.getX() + step, r.getBottom(), r.getX() + step * 3, r.getY(), r.getX() + step * 3, r.getY() + r.getHeight() * 0.7f);
            cord.startNewSubPath (r.getX() + step * 2, r.getY() + r.getHeight() * 0.3f);
            cord.cubicTo (r.getX() + step * 2, r.getBottom() + 4.0f, r.getX() + step * 4, r.getY() + 4.0f, r.getX() + step * 4, r.getY() + r.getHeight() * 0.7f);
            g.strokePath (cord, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        audioTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // an interface: a box with two inputs and a meter
            auto box = r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.56f);
            g.setColour (Colours::panelHi);
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (Colours::textDim);
            for (int c = 0; c < 2; ++c)
                g.drawEllipse (box.getX() + box.getWidth() * (0.18f + 0.22f * c) - 7.0f, box.getCentreY() - 7.0f, 14.0f, 14.0f, 2.0f);
            g.setColour (Colours::accent);
            for (int k = 0; k < 5; ++k)
                g.fillRect (box.getX() + box.getWidth() * 0.66f + k * 6.0f, box.getCentreY() + 8.0f - k * 4.0f, 4.0f, 4.0f + k * 4.0f);
        };
        netTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // three machines linked
            const juce::Point<float> a { r.getCentreX(), r.getY() + r.getHeight() * 0.22f },
                                     b { r.getX() + r.getWidth() * 0.2f, r.getBottom() - r.getHeight() * 0.22f },
                                     c { r.getRight() - r.getWidth() * 0.2f, r.getBottom() - r.getHeight() * 0.22f };
            g.setColour (Colours::textDim);
            for (auto [p, q] : { std::pair { a, b }, std::pair { a, c }, std::pair { b, c } }) g.drawLine ({ p, q }, 2.0f);
            for (auto p : { a, b, c })
            {
                g.setColour (Colours::panelHi);
                g.fillRoundedRectangle (juce::Rectangle<float> (16, 12).withCentre (p), 2.0f);
                g.setColour (Colours::forPart ("network"));
                g.drawRoundedRectangle (juce::Rectangle<float> (16, 12).withCentre (p), 2.0f, 1.5f);
            }
        };
        netTile.onClick = [this] { if (onShowNetwork) onShowNetwork(); };
        netTile.setTooltip ("NETWORK: Hawthorne Link - audio from other PCs, onto the patchbay");
        addAndMakeVisible (netTile);
        patchTile.onClick = [this] { showPatchbay(); };
        audioTile.onClick = [this] { showAudio(); };
        patchTile.setTooltip ("PATCHBAY: what goes where between the parts and the interface");
        audioTile.setTooltip ("INTERFACE: the audio interface, sample rate and buffer");
        addAndMakeVisible (patchTile);
        addAndMakeVisible (audioTile);

        startTimerHz (4);
        setSize (wordmarkWidth + (tiles.size() + 3) * (tileWidth + gap) + 26 + 260, barHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::panel);
        auto r = getLocalBounds().reduced (14, 0);
        g.setColour (Colours::accent);
        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
        g.drawText ("MELVILLE", r.removeFromLeft (wordmarkWidth - 14), juce::Justification::centredLeft);

        // a divider between the parts and the studio controls
        g.setColour (Colours::line);
        g.drawVerticalLine (dividerX, 14.0f, (float) getHeight() - 14.0f);

        if (statusArea.getWidth() > 60)
        {
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.setColour (Colours::textDim);
            auto* dev = hub.deviceManager.getCurrentAudioDevice();
            juce::StringArray lines;
            if (dev == nullptr) lines.add ("NO INTERFACE - CLICK INTERFACE");
            else
            {
                lines.add (dev->getName().toUpperCase());
                lines.add (juce::String (dev->getCurrentSampleRate() / 1000.0, 1) + " kHz  \xc2\xb7  " + juce::String (dev->getCurrentBufferSizeSamples())
                           + "  \xc2\xb7  CPU " + juce::String (juce::roundToInt (hub.getLoad() * 100.0f)) + "%");
            }
            g.drawFittedText (lines.joinIntoString ("\n"), statusArea, juce::Justification::centredRight, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 6);
        r.removeFromLeft (wordmarkWidth - 14);
        for (auto* t : tiles) { t->setBounds (r.removeFromLeft (tileWidth)); r.removeFromLeft (gap); }
        r.removeFromLeft (6);
        dividerX = r.getX();
        r.removeFromLeft (14);
        for (auto* t : { &patchTile, &netTile, &audioTile }) { t->setBounds (r.removeFromLeft (tileWidth)); r.removeFromLeft (gap); }
        statusArea = r.withTrimmedLeft (10);
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

    static constexpr int barHeight = 100, tileWidth = 84, gap = 4, wordmarkWidth = 160;

private:
    void timerCallback() override { repaint(); }

    Hub& hub;
    std::vector<std::unique_ptr<PartUI>>& parts;
    std::vector<std::unique_ptr<PartWindow>>& windows;
    juce::OwnedArray<Tile> tiles;
    Tile patchTile { "Patchbay", {}, Colours::accent }, netTile { "Network", {}, Colours::accent }, audioTile { "Interface", {}, Colours::accent };
public:
    std::function<void()> onShowNetwork;
private:
    juce::Rectangle<int> statusArea;
    int dividerX = 0;
    std::unique_ptr<juce::DocumentWindow> patchWindow;
};

class BarWindow : public juce::DocumentWindow
{
public:
    explicit BarWindow (Bar* bar) : DocumentWindow ("MELVILLE", Colours::panel, DocumentWindow::minimiseButton | DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (bar, true);
        setResizable (false, false);
        // at the top centre of the main screen
        auto area = juce::Rectangle<int> (0, 0, 1920, 1080);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userBounds.toNearestInt();
        setTopLeftPosition (area.getCentreX() - getWidth() / 2, area.getY() + 8);
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

    void initialise (const juce::String& commandLine) override
    {
        using namespace melville;
        taskbar::setProcessIdentity();
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

        // Hawthorne Link, now MELVILLE's (the first time with the senders Hawthorne had)
        {
            juce::PropertiesFile::Options ho;
            ho.applicationName = "Hawthorne";
            ho.filenameSuffix = ".settings";
            ho.folderName = "Hawthorne";
            ho.osxLibrarySubFolder = "Application Support";
            juce::PropertiesFile hawthorneSettings (ho);
            hub->getNetwork().start (*props.getUserSettings(), &hawthorneSettings);
        }

        // every part, running from the start (windows open from the bar)
        parts.push_back (parts::makeHawthorne());
        parts.push_back (parts::makeEmerson());
        parts.push_back (parts::makeWhitman());
        parts.push_back (parts::makeThoreau());
        parts.push_back (parts::makePequod());
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);   // (Hawthorne sets its own as the default)

        // every part gets its own taskbar button and icon, in the same order every time:
        // Hawthorne up front, the rest down on the taskbar ready to click
        for (auto& p : parts)
        {
            windows.push_back (std::make_unique<PartWindow> (*p));
            taskbar::setWindowIdentity (*windows.back(), p->id, p->title, loadIcon (p->id, 256));
        }
        auto* barContent = new Bar (*hub, parts, windows);
        barContent->onShowNetwork = [this] { showNetwork(); };
        bar = std::make_unique<BarWindow> (barContent);
        for (size_t i = 1; i < windows.size(); ++i)
        {
            windows[i]->show();
            windows[i]->setMinimised (true);
        }
        windows.front()->show();   // Hawthorne, as you'd start the day
        showFromCommandLine (commandLine);

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
            hub->getNetwork().saveSettings (*props.getUserSettings());
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

        networkWindow = nullptr;
        bar = nullptr;
        windows.clear();
        while (! parts.empty()) parts.pop_back();               // the parts close their devices
        hub = nullptr;
        props.closeFiles();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void showNetwork()
    {
        if (networkWindow == nullptr)
            networkWindow = melville::parts::makeNetworkWindow (hub->getNetwork().getReceiver(), [this]
            {
                hub->getNetwork().saveSettings (*props.getUserSettings());
                props.saveIfNeeded();
            });
        networkWindow->setVisible (true);
        networkWindow->toFront (true);
    }

    void systemRequestedQuit() override { quit(); }
    // a pinned part's taskbar button runs MELVILLE.exe --show=<part>: bring that part up
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (! showFromCommandLine (commandLine) && bar != nullptr) bar->toFront (true);
    }

    bool showFromCommandLine (const juce::String& commandLine)
    {
        const auto id = commandLine.fromFirstOccurrenceOf ("--show=", false, true).upToFirstOccurrenceOf (" ", false, false).unquoted().trim();
        for (size_t i = 0; id.isNotEmpty() && i < parts.size(); ++i)
            if (parts[i]->id.equalsIgnoreCase (id)) { windows[i]->show(); return true; }
        return false;
    }

private:
    juce::ApplicationProperties props;
    melville::MelvilleLookAndFeel lookAndFeel;
    std::unique_ptr<melville::Hub> hub;
    std::vector<std::unique_ptr<melville::PartUI>> parts;
    std::vector<std::unique_ptr<melville::PartWindow>> windows;
    std::unique_ptr<melville::BarWindow> bar;
    std::unique_ptr<juce::DocumentWindow> networkWindow;
};

START_JUCE_APPLICATION (MelvilleApplication)
