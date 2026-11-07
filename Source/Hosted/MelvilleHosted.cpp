#include "MelvilleHosted.h"
#include "../Core/Hub.h"
#include "../Core/PartDevice.h"
#include "../Core/NetworkNode.h"
#include "MelvilleNetwork.h"

namespace melville
{

bool isHosted() noexcept { return Hub::getIfExists() != nullptr; }

bool attach (juce::AudioDeviceManager& dm, const char* partId)
{
    auto* hub = Hub::getIfExists();
    if (hub == nullptr) return false;
    auto* part = hub->findPart (partId);
    if (part == nullptr) { jassertfalse; return false; }

    part->manager = &dm;
    // only MELVILLE's type: the manager never scans for (or grabs) the interface itself
    dm.addAudioDeviceType (std::make_unique<PartDeviceType> (*hub, part->port.id));
    dm.initialise ((int) part->visibleIns.size(), (int) part->visibleOuts.size(), nullptr, false, part->deviceName);
    dm.setCurrentAudioDeviceType (PartDeviceType::typeName, true);

    auto setup = dm.getAudioDeviceSetup();
    setup.inputDeviceName = setup.outputDeviceName = part->deviceName;
    setup.sampleRate = hub->getSampleRate();
    setup.bufferSize = hub->getBlockSize();
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange (0, (int) part->visibleIns.size(), true);
    setup.outputChannels.setRange (0, (int) part->visibleOuts.size(), true);
    const auto error = dm.setAudioDeviceSetup (setup, true);
    jassert (error.isEmpty());
    return true;
}

hawthorne::NetReceiver* networkReceiver() noexcept
{
    auto* hub = Hub::getIfExists();
    return hub != nullptr ? &hub->getNetwork().getReceiver() : nullptr;
}

namespace
{
    // a tick box per MIDI input on this computer, for the part's own MIDI
    class MidiInputsPanel : public juce::Component, private juce::Timer
    {
    public:
        explicit MidiInputsPanel (juce::AudioDeviceManager& d) : dm (d)
        {
            note.setText ("The audio device, rate and buffer are MELVILLE's: click INTERFACE on the MELVILLE bar.",
                          juce::dontSendNotification);
            note.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (note);
            caption.setText ("MIDI INPUTS", juce::dontSendNotification);
            caption.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            addAndMakeVisible (caption);
            refresh();
            startTimer (1500);   // inputs plugged in or out
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            note.setBounds (r.removeFromTop (36));
            r.removeFromTop (6);
            caption.setBounds (r.removeFromTop (22));
            for (auto* b : boxes) b->setBounds (r.removeFromTop (26));
        }

    private:
        void refresh()
        {
            const auto devices = juce::MidiInput::getAvailableDevices();
            juce::StringArray ids;
            for (auto& d : devices) ids.add (d.identifier);
            if (ids == shown) return;
            shown = ids;
            boxes.clear();
            for (auto& d : devices)
            {
                auto* b = boxes.add (new juce::ToggleButton (d.name));
                b->setToggleState (dm.isMidiInputDeviceEnabled (d.identifier), juce::dontSendNotification);
                const auto id = d.identifier;
                b->onClick = [this, b, id] { dm.setMidiInputDeviceEnabled (id, b->getToggleState()); };
                addAndMakeVisible (b);
            }
            if (devices.isEmpty())
            {
                auto* b = boxes.add (new juce::ToggleButton ("(no MIDI inputs found)"));
                b->setEnabled (false);
                addAndMakeVisible (b);
            }
            resized();
        }
        void timerCallback() override { refresh(); }

        juce::AudioDeviceManager& dm;
        juce::Label note, caption;
        juce::OwnedArray<juce::ToggleButton> boxes;
        juce::StringArray shown;
    };
}

std::unique_ptr<juce::Component> midiInputsPanel (juce::AudioDeviceManager& dm)
{
    return std::make_unique<MidiInputsPanel> (dm);
}

void detach (juce::AudioDeviceManager& dm)
{
    if (auto* hub = Hub::getIfExists())
        for (auto& n : hub->getNodes())
            if (auto* p = dynamic_cast<PartNode*> (n.get()))
                if (p->manager == &dm) p->manager = nullptr;
}

} // namespace melville
