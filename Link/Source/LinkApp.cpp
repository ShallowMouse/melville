// MELVILLE Link: the small program for a PC that isn't running MELVILLE. It sends that PC's
// interface inputs to MELVILLE on another PC over the network, and plays what a MELVILLE
// sends it (its LINK SEND jacks) on that PC's interface outputs. Both ways at once, or either.

#include <juce_audio_utils/juce_audio_utils.h>
#include "NetSender.h"
#include "NetReceiver.h"
#include "../../Source/UI/MelvilleLookAndFeel.h"

namespace melville::link
{

using hawthorne::net::maxChannels;

//==============================================================================
class LinkComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit LinkComponent (juce::ApplicationProperties& p) : props (p)
    {
        setLookAndFeel (&lnf);
        juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
        auto* s = props.getUserSettings();

        // --- send: this PC's inputs
        sender.setRedundancy (s->getBoolValue ("redundancy", true));
        sender.setName (s->getValue ("name", "MELVILLE Link - " + juce::SystemStats::getComputerName()));
        sendButton.setClickingTogglesState (true);
        sendButton.setTooltip ("Send this PC's interface inputs (the ones switched on below) to MELVILLE on another PC");
        sendButton.onClick = [this] { setSending (sendButton.getToggleState()); };
        nameEditor.setText (sender.getName(), juce::dontSendNotification);
        nameEditor.setTooltip ("How this PC appears in MELVILLE's LINK window");
        nameEditor.onTextChange = [this] { sender.setName (nameEditor.getText()); props.getUserSettings()->setValue ("name", sender.getName()); };
        redundancy.setToggleState (sender.getRedundancy(), juce::dontSendNotification);
        redundancy.setTooltip ("Every packet also carries the one before, so a lost packet costs nothing (twice the bandwidth)");
        redundancy.onClick = [this]
        {
            sender.setRedundancy (redundancy.getToggleState());
            props.getUserSettings()->setValue ("redundancy", redundancy.getToggleState());
            restartAudio();          // (takes effect on a new session)
        };

        // --- receive: a MELVILLE's LINK SEND, onto this PC's outputs
        receiver.ignoreSender (sender.getSenderId());
        receiver.setManualAddresses (juce::StringArray::fromLines (s->getValue ("manual")));
        juce::String err;
        receiveOk = receiver.start (err);
        fromBox.setTooltip ("Which MELVILLE to play here: its LINK SEND channels come out of this PC's outputs, 1, 2, 3 ...");
        fromBox.onChange = [this] { pickSource(); };
        addressButton.setTooltip ("A MELVILLE on another subnet or VLAN: type its PC's address");
        addressButton.onClick = [this] { askAddress(); };
        if (auto from = s->getValue ("receiveFrom"); from.isNotEmpty()) receiver.connect (0, from);

        for (auto* c : std::initializer_list<juce::Component*> { &sendButton, &nameEditor, &redundancy, &fromBox, &addressButton })
            addAndMakeVisible (c);

        auto saved = s->getXmlValue ("audioDeviceState");
        deviceManager.initialise (8, 8, saved.get(), true);
        deviceManager.addChangeListener (this);
        deviceManager.addAudioCallback (this);
        selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager, 0, maxChannels, 0, maxChannels, false, false, false, false);
        addAndMakeVisible (*selector);

        if (s->getBoolValue ("sending", true)) { sendButton.setToggleState (true, juce::dontSendNotification); setSending (true); }
        refreshSources();
        startTimerHz (20);
        setSize (980, 760);
    }

    ~LinkComponent() override
    {
        deviceManager.removeAudioCallback (this);
        deviceManager.removeChangeListener (this);
        deviceManager.closeAudioDevice();
        sender.stop();
        receiver.stop();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace melville::Colours;
        g.fillAll (bg);
        g.setColour (panel);
        g.fillRect (header);
        g.setColour (accent);
        g.fillRect (header.withTop (header.getBottom() - 2));
        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
        g.drawText ("MELVILLE LINK", header.withTrimmedLeft (20), juce::Justification::centredLeft);
        g.setColour (textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("v" + juce::JUCEApplication::getInstance()->getApplicationVersion() + "   audio to and from MELVILLE over the network", header.withTrimmedRight (20),
                    juce::Justification::centredRight);

        auto section = [&] (juce::Rectangle<int> r, const juce::String& title)
        {
            g.setColour (panel);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (accent);
            g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
            g.drawText (title, r.reduced (14, 8).removeFromTop (18), juce::Justification::centredLeft);
        };
        section (sendArea, "SEND  this PC's inputs to MELVILLE");
        section (receiveArea, "RECEIVE  a MELVILLE's LINK SEND on this PC's outputs");

        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.setColour (textDim);
        g.drawText ("NAME", nameEditor.getBounds().translated (-56, 0).withWidth (50), juce::Justification::centredRight);
        g.drawText ("FROM", fromBox.getBounds().translated (-56, 0).withWidth (50), juce::Justification::centredRight);

        auto* dev = deviceManager.getCurrentAudioDevice();

        // send status
        {
            auto st = sendStatus;
            const auto subs = sender.getSubscribers();
            juce::String line;
            juce::Colour c = text;
            if (! sendButton.getToggleState())  { line = "Off"; c = textDim; }
            else if (! sendOk)                  { line = "The network couldn't be opened for sending (is another sender running on this PC?)"; c = warn; }
            else if (dev == nullptr)            { line = "No audio device: choose one below"; c = warn; }
            else if (subs.empty())              line = juce::String (sendChannels.load()) + " inputs ready: connect \"" + sender.getName() + "\" in MELVILLE's LINK window";
            else
            {
                juce::StringArray to;
                for (auto& s : subs) to.add (s.address);
                line = "Sending " + juce::String (sendChannels.load()) + " inputs to " + to.joinIntoString (", ");
            }
            g.setColour (c);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (line, st.removeFromTop (20), juce::Justification::centredLeft, true);
            if (sender.getQueueOverruns() > 0)
            {
                g.setColour (warn);
                g.drawText ("The network couldn't keep up: " + juce::String (sender.getQueueOverruns()) + " packets dropped", st.removeFromTop (18),
                            juce::Justification::centredLeft, true);
            }
            meters (g, sendMeters, inMeters, sendChannels.load());
        }

        // receive status
        {
            auto st = receiveStatus;
            const auto s = receiver.getStatus (0);
            juce::String line;
            juce::Colour c = text;
            if (! receiveOk)                                         { line = "The network couldn't be opened for receiving"; c = warn; }
            else if (s.state == hawthorne::NetReceiver::State::off) { line = "Not receiving: pick a MELVILLE above"; c = textDim; }
            else
            {
                line = s.name + ": " + hawthorne::NetReceiver::describe (s.state);
                if (s.channels > 0)
                    line << "   " << s.channels << " ch to outputs 1-" << s.channels << "   buffer " << juce::String (s.bufferMs, 1) << " ms";
                if (s.state == hawthorne::NetReceiver::State::corrected) line << "   (" << juce::String (s.ppm, 1) << " ppm: word clock the two interfaces to be bit-perfect)";
                if (s.state == hawthorne::NetReceiver::State::rateMismatch) c = warn;
                if (s.dropouts > 0) line << "   dropouts " << s.dropouts;
            }
            g.setColour (c);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (line, st.removeFromTop (20), juce::Justification::centredLeft, true);
            meters (g, receiveMeters, outMeters, juce::jmax (0, s.channels));
        }

        if (dev != nullptr)
        {
            g.setColour (textDim);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText (dev->getTypeName() + ": " + dev->getName() + "   " + juce::String (dev->getCurrentSampleRate() / 1000.0, 1) + " kHz   "
                            + juce::String (dev->getCurrentBufferSizeSamples()) + " samples. Run it at the same rate as MELVILLE (and word clock "
                            "the interfaces together for bit-perfect audio).",
                        deviceNote, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header = r.removeFromTop (52);
        r.reduce (16, 12);

        sendArea = r.removeFromTop (150);
        {
            auto a = sendArea.reduced (14, 8);
            a.removeFromTop (24);
            auto row = a.removeFromTop (28);
            sendButton.setBounds (row.removeFromLeft (90));
            row.removeFromLeft (70);
            nameEditor.setBounds (row.removeFromLeft (300));
            row.removeFromLeft (16);
            redundancy.setBounds (row.removeFromLeft (200));
            a.removeFromTop (8);
            sendStatus = a.removeFromTop (40);
            sendMeters = a;
        }
        r.removeFromTop (10);
        receiveArea = r.removeFromTop (150);
        {
            auto a = receiveArea.reduced (14, 8);
            a.removeFromTop (24);
            auto row = a.removeFromTop (28);
            row.removeFromLeft (60);
            fromBox.setBounds (row.removeFromLeft (360));
            row.removeFromLeft (10);
            addressButton.setBounds (row.removeFromLeft (170));
            a.removeFromTop (8);
            receiveStatus = a.removeFromTop (40);
            receiveMeters = a;
        }
        r.removeFromTop (8);
        deviceNote = r.removeFromTop (22);
        selector->setBounds (r);
    }

private:
    void setSending (bool on)
    {
        if (on && ! sending)
        {
            juce::String err;
            sendOk = sender.start (sender.getName(), err);
            sending = sendOk;
        }
        else if (! on && sending)
        {
            sender.stop();
            sending = false;
        }
        props.getUserSettings()->setValue ("sending", on);
        repaint();
    }

    void refreshSources()
    {
        const auto senders = receiver.getSenders();
        juce::StringArray names;
        for (auto& s : senders) names.add (s.name);
        const auto current = receiver.getSlotName (0);
        if (current.isNotEmpty()) names.addIfNotAlreadyThere (current);
        names.sort (true);
        if (names == sourceNames) return;
        sourceNames = names;
        fromBox.clear (juce::dontSendNotification);
        fromBox.addItem ("(nothing)", 1);
        for (int i = 0; i < names.size(); ++i) fromBox.addItem (names[i], i + 2);
        fromBox.setSelectedId (current.isEmpty() ? 1 : names.indexOf (current) + 2, juce::dontSendNotification);
    }

    void pickSource()
    {
        const auto id = fromBox.getSelectedId();
        if (id <= 1) { receiver.disconnect (0); props.getUserSettings()->setValue ("receiveFrom", juce::String()); }
        else
        {
            const auto name = sourceNames[id - 2];
            receiver.connect (0, name);
            props.getUserSettings()->setValue ("receiveFrom", name);
        }
        repaint();
    }

    void askAddress()
    {
        auto* w = new juce::AlertWindow ("MELVILLE on another network", "The address of the PC running MELVILLE (its LINK SEND on).",
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("ip", receiver.getManualAddresses().joinIntoString (", "), "Address");
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1)
            {
                auto ips = juce::StringArray::fromTokens (w->getTextEditorContents ("ip"), ", ", "");
                ips.removeEmptyStrings();
                receiver.setManualAddresses (ips);
                props.getUserSettings()->setValue ("manual", ips.joinIntoString ("\n"));
            }
        }), true);
    }

    void meters (juce::Graphics& g, juce::Rectangle<int> area, const std::array<float, maxChannels>& m, int n)
    {
        if (n <= 0 || area.isEmpty()) return;
        const float w = juce::jmin (36.0f, (float) area.getWidth() / (float) n);
        for (int ch = 0; ch < n; ++ch)
        {
            auto r = juce::Rectangle<float> ((float) area.getX() + ch * w, (float) area.getY(), juce::jmax (2.0f, w - 3.0f), (float) area.getHeight());
            g.setColour (juce::Colours::black);
            g.fillRect (r);
            const float level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (m[(size_t) ch], -60.0f) + 60.0f) / 60.0f);
            g.setColour (m[(size_t) ch] > 0.89f ? melville::Colours::warn : melville::Colours::accent);
            g.fillRect (r.withTrimmedTop (r.getHeight() * (1.0f - level)));
        }
    }

    // --- audio
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        const int ch = juce::jlimit (1, maxChannels, d->getActiveInputChannels().countNumberOfSetBits());
        sendChannels = ch;
        sender.prepare (d->getCurrentSampleRate(), ch, d->getInputLatencyInSamples() + d->getCurrentBufferSizeSamples());
        receiver.prepare (d->getCurrentSampleRate());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        if (sending) sender.push (in, numIn, n);
        peaksOf (in, juce::jmin (numIn, maxChannels), n, inPeaks);

        // what a MELVILLE sends, onto the outputs in order
        for (int done = 0; done < n;)
        {
            const int chunk = juce::jmin (n - done, hawthorne::NetReceiver::maxBlock);
            receiver.process (chunk);
            for (int o = 0; o < numOut; ++o)
            {
                if (out[o] == nullptr) continue;
                if (const float* src = receiver.getChannel (0, o)) juce::FloatVectorOperations::copy (out[o] + done, src, chunk);
                else juce::FloatVectorOperations::clear (out[o] + done, chunk);
            }
            done += chunk;
        }
        peaksOf (out, juce::jmin (numOut, maxChannels), n, outPeaks);
    }

    static void peaksOf (const float* const* ch, int num, int n, std::array<std::atomic<float>, maxChannels>& peaks) noexcept
    {
        for (int c = 0; c < num; ++c)
            if (ch[c] != nullptr)
            {
                const auto mm = juce::FloatVectorOperations::findMinAndMax (ch[c], n);
                const float p = juce::jmax (-mm.getStart(), mm.getEnd());
                if (p > peaks[(size_t) c].load (std::memory_order_relaxed)) peaks[(size_t) c].store (p, std::memory_order_relaxed);
            }
    }

    void restartAudio()
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        deviceManager.setAudioDeviceSetup (setup, true);
        deviceManager.restartLastAudioDevice();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (auto xml = deviceManager.createStateXml())
        {
            props.getUserSettings()->setValue ("audioDeviceState", xml.get());
            props.saveIfNeeded();
        }
        repaint();
    }

    void timerCallback() override
    {
        for (size_t c = 0; c < (size_t) maxChannels; ++c)
        {
            inMeters[c] = juce::jmax (inPeaks[c].exchange (0.0f), inMeters[c] * 0.85f);
            outMeters[c] = juce::jmax (outPeaks[c].exchange (0.0f), outMeters[c] * 0.85f);
        }
        if (++tick % 10 == 0) refreshSources();
        repaint (sendArea);
        repaint (receiveArea);
    }

    juce::ApplicationProperties& props;
    melville::MelvilleLookAndFeel lnf;
    juce::AudioDeviceManager deviceManager;
    hawthorne::NetSender sender;
    hawthorne::NetReceiver receiver;
    std::atomic<bool> sending { false };
    bool sendOk = true, receiveOk = false;
    std::atomic<int> sendChannels { 0 };
    std::array<std::atomic<float>, maxChannels> inPeaks {}, outPeaks {};
    std::array<float, maxChannels> inMeters {}, outMeters {};
    juce::StringArray sourceNames { "(not yet listed)" };    // (so the first refresh fills the list)
    int tick = 0;

    juce::TextButton sendButton { "SEND" };
    juce::TextEditor nameEditor;
    juce::ToggleButton redundancy { "Redundant packets" };
    juce::ComboBox fromBox;
    juce::TextButton addressButton { "ADD BY ADDRESS..." };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    juce::Rectangle<int> header, sendArea, receiveArea, sendStatus, receiveStatus, sendMeters, receiveMeters, deviceNote;
};

//==============================================================================
class LinkApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MELVILLE Link"; }
    const juce::String getApplicationVersion() override { return MELVILLE_LINK_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "MELVILLE Link";
        o.filenameSuffix = ".settings";
        o.folderName = "MELVILLE";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);
        window = std::make_unique<Window> (props);

        // (development: MELVILLE_LINK_SNAPSHOT=<file.png> saves a picture of the window and quits)
        if (auto snap = juce::SystemStats::getEnvironmentVariable ("MELVILLE_LINK_SNAPSHOT", {}); snap.isNotEmpty())
            juce::Timer::callAfterDelay (2500, [this, snap]
            {
                auto img = window->getContentComponent()->createComponentSnapshot (window->getContentComponent()->getLocalBounds());
                juce::FileOutputStream os { juce::File (snap) };
                if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
                quit();
            });
    }

    void shutdown() override { window = nullptr; props.saveIfNeeded(); }
    void systemRequestedQuit() override { quit(); }

private:
    struct Window : juce::DocumentWindow
    {
        explicit Window (juce::ApplicationProperties& p)
            : juce::DocumentWindow ("MELVILLE Link", melville::Colours::bg, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new LinkComponent (p), true);
            setResizable (true, false);
            setResizeLimits (760, 600, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    juce::ApplicationProperties props;
    std::unique_ptr<Window> window;
};

} // namespace melville::link

START_JUCE_APPLICATION (melville::link::LinkApplication)
