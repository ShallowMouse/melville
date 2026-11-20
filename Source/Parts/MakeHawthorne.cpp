// Hawthorne inside MELVILLE (compiled with Hawthorne's settings).
#include "Parts.h"
#include "UI/MainComponent.h"
#include "UI/NetworkWindow.h"
#include "Network/NetSender.h"
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
    /** MELVILLE Link's sending half: on / off, its name on the network, how many channels
        (the LINK SEND jacks on the patchbay), and who's receiving it. */
    struct SendStrip : juce::Component, private juce::Timer
    {
        SendStrip (LinkSend s, std::function<void()> changed) : send (std::move (s)), onChanged (std::move (changed))
        {
            onButton.setClickingTogglesState (true);
            onButton.setToggleState (send.isOn(), juce::dontSendNotification);
            onButton.setTooltip ("Send what's cabled to LINK SEND on the patchbay to MELVILLE (or MELVILLE Link) on other PCs");
            onButton.onClick = [this]
            {
                juce::String err;
                if (! send.setOn (onButton.getToggleState(), err)) { onButton.setToggleState (false, juce::dontSendNotification); problem = err; }
                else problem.clear();
                onChanged();
                repaint();
            };
            name.setText (send.sender->getName(), juce::dontSendNotification);
            name.setTooltip ("How this MELVILLE appears on the other PCs");
            name.onTextChange = [this] { send.sender->setName (name.getText()); onChanged(); };
            for (int c : { 2, 4, 6, 8, 12, 16 }) chans.addItem (juce::String (c) + " CHANNELS", c);
            chans.setSelectedId (send.channels(), juce::dontSendNotification);
            if (chans.getSelectedId() == 0) chans.setSelectedId (8, juce::dontSendNotification);
            chans.onChange = [this] { send.setChannels (chans.getSelectedId()); onChanged(); };
            redundancy.setToggleState (send.sender->getRedundancy(), juce::dontSendNotification);
            redundancy.setTooltip ("Every packet also carries the one before, so a lost packet costs nothing (twice the bandwidth). From the next session.");
            redundancy.onClick = [this] { send.sender->setRedundancy (redundancy.getToggleState()); onChanged(); };
            for (auto* c : std::initializer_list<juce::Component*> { &onButton, &name, &chans, &redundancy }) addAndMakeVisible (c);
            startTimerHz (4);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (20, 0);
            r.removeFromTop (34);
            auto row = r.removeFromTop (30);
            onButton.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (10);
            name.setBounds (row.removeFromLeft (280));
            row.removeFromLeft (10);
            chans.setBounds (row.removeFromLeft (150));
            row.removeFromLeft (10);
            redundancy.setBounds (row.removeFromLeft (160));
            statusArea = r.removeFromTop (24);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().reduced (20, 0);
            g.setColour (melville::Colours::accent);
            g.setFont (hawthorne::monoFont (13.0f, true));
            g.drawText ("SEND", r.removeFromTop (30).withTrimmedTop (8), juce::Justification::centredLeft);
            g.setFont (hawthorne::uiFont (12.5f));
            g.setColour (problem.isNotEmpty() ? juce::Colours::orangered : hawthorne::Colours::textDim);
            juce::String s;
            if (problem.isNotEmpty()) s = problem;
            else if (! send.isOn()) s = "Off. Cable anything to LINK SEND on the patchbay (a mix, a click, tracks) and turn it on to send it to another PC.";
            else
            {
                const auto subs = send.sender->getSubscribers();
                s << send.channels() << " channels from LINK SEND, as \"" << send.sender->getName() << "\". ";
                if (subs.empty()) s << "Waiting for a receiver: connect it in MELVILLE LINK on the other PC.";
                else
                {
                    juce::StringArray who;
                    for (auto& sub : subs) who.add (sub.address);
                    s << "Sending to " << who.joinIntoString (", ") << ".";
                }
            }
            g.drawText (s, statusArea, juce::Justification::centredLeft, true);
            g.setColour (hawthorne::Colours::textDim.withAlpha (0.3f));
            g.drawHorizontalLine (getHeight() - 1, 20.0f, (float) getWidth() - 20.0f);
        }

        void timerCallback() override { repaint (statusArea); }

        LinkSend send;
        std::function<void()> onChanged;
        juce::TextButton onButton { "SEND" };
        juce::TextEditor name;
        juce::ComboBox chans;
        juce::ToggleButton redundancy { "REDUNDANT PACKETS" };
        juce::Rectangle<int> statusArea;
        juce::String problem;
    };

    /** The window's content: SEND above, the receiving half (Hawthorne Link's panel) below. */
    struct LinkContent : juce::Component
    {
        LinkContent (hawthorne::NetworkPanel& p, LinkSend s, std::function<void()> changed) : panel (p), strip (std::move (s), std::move (changed))
        {
            addAndMakeVisible (strip);
            addAndMakeVisible (panel);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            strip.setBounds (r.removeFromTop (104));
            panel.setBounds (r);
        }
        void paint (juce::Graphics& g) override { g.fillAll (hawthorne::Colours::panel); }
        hawthorne::NetworkPanel& panel;
        SendStrip strip;
    };

    // Hawthorne Link's window, as MELVILLE's NETWORK window (with Hawthorne's look for its controls)
    struct MelvilleNetworkWindow : hawthorne::NetworkWindow
    {
        MelvilleNetworkWindow (hawthorne::NetReceiver& rx, LinkSend send, std::function<void()> changed)
            : hawthorne::NetworkWindow (rx), content (getPanel(), std::move (send), std::move (changed))
        {
            setName (juce::String (juce::CharPointer_UTF8 ("MELVILLE \xc2\xb7 MELVILLE LINK")));
            setLookAndFeel (&lnf);
            // MELVILLE's bar and colour (it's MELVILLE's link, not Hawthorne's)
            getPanel().setAccent (melville::Colours::accent, melville::Colours::accentLo);
            for (auto id : { (int) juce::Slider::thumbColourId, (int) juce::Slider::trackColourId, (int) juce::TextButton::buttonOnColourId,
                             (int) juce::Label::outlineWhenEditingColourId, (int) juce::TextEditor::focusedOutlineColourId, (int) juce::CaretComponent::caretColourId })
                lnf.setColour (id, melville::Colours::accent);
            clearContentComponent();
            frameWindow (*this, &content, false, "MELVILLE Link", melville::Colours::accent,
                         juce::ImageCache::getFromMemory (BinaryData::MELVILLE_png, BinaryData::MELVILLE_pngSize).rescaled (64, 64, juce::Graphics::highResamplingQuality),
                         WindowTitleBar::minimise | WindowTitleBar::close);
            setResizable (true, false);
            setResizeLimits (820, 520 + WindowTitleBar::height, 2000, 1400);
            centreWithSize (920, 660 + WindowTitleBar::height);
        }
        ~MelvilleNetworkWindow() override { clearContentComponent(); setLookAndFeel (nullptr); }
        LinkContent content;
        hawthorne::HawthorneLookAndFeel lnf;
    };
}

std::unique_ptr<juce::DocumentWindow> makeNetworkWindow (hawthorne::NetReceiver& rx, LinkSend send, std::function<void()> onChanged)
{
    auto w = std::make_unique<MelvilleNetworkWindow> (rx, std::move (send), onChanged);
    w->getPanel().onChanged = std::move (onChanged);
    return w;
}

} // namespace melville::parts
