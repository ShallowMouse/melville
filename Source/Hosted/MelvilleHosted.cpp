#include "MelvilleHosted.h"
#include "../Core/Hub.h"
#include "../Core/PartDevice.h"
#include "../Core/NetworkNode.h"
#include "MelvilleNetwork.h"

namespace melville
{

namespace
{
    juce::String nextPart;
    std::map<juce::String, SessionPart> sessions;
    std::map<juce::String, juce::AudioDeviceManager*> sessionOwners;
}

void addToSession (juce::AudioDeviceManager& dm, SessionPart sp)
{
    if (auto* hub = Hub::getIfExists())
        for (auto& n : hub->getNodes())
            if (auto* p = dynamic_cast<PartNode*> (n.get()))
                if (p->manager == &dm) { sessions[p->port.id] = std::move (sp); sessionOwners[p->port.id] = &dm; return; }
}

const std::map<juce::String, SessionPart>& sessionParts() { return sessions; }

bool isHosted() noexcept { return Hub::getIfExists() != nullptr; }

void setNextPart (const juce::String& partId) { nextPart = partId; }

bool attach (juce::AudioDeviceManager& dm, const char* partId)
{
    auto* hub = Hub::getIfExists();
    if (hub == nullptr) return false;
    auto* part = hub->findPart (nextPart.isNotEmpty() ? nextPart : juce::String (partId));
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

Tracks& tracks() noexcept
{
    static Tracks t;
    return t;
}

Library& library() noexcept
{
    static Library l;
    return l;
}

juce::File studioFolder()
{
    auto& l = library();
    const auto reel = l.currentReel ? l.currentReel() : juce::File();
    return reel.existsAsFile() ? reel.getParentDirectory() : juce::File();
}

Transport& transport() noexcept
{
    static Transport t;
    return t;
}

void postNote (int channel, int note, float velocity, int offset) noexcept
{
    if (auto* h = Hub::getIfExists())
        if (auto* m = h->getMidi()) m->postStep (channel, note, velocity, offset);
}

int midiJack (const juce::String& name)
{
    if (auto* h = Hub::getIfExists())
        if (auto* p = h->getPatchbay().findPort ("midi"))
            return p->dests.indexOf (name);
    return -1;
}

void takeMidi (int jack, juce::MidiBuffer& into) noexcept
{
    if (jack < 0) return;
    if (auto* h = Hub::getIfExists())
        if (auto* m = h->getMidi()) m->take (jack, into);
}

juce::String midiCabledInto (int jack)
{
    juce::StringArray from;
    if (auto* h = Hub::getIfExists())
        if (auto* p = h->getPatchbay().findPort ("midi"); p != nullptr && jack >= 0)
            for (auto& [d, c] : h->getPatchbay().getLiveCords())
                if (d == p->firstDest + jack && c.source >= p->firstSource && c.source < p->firstSource + p->sources.size())
                    from.add (p->sources[c.source - p->firstSource].replace ("In: ", ""));
    return from.joinIntoString (", ");
}

void setStepLength (int n)
{
    if (auto* h = Hub::getIfExists())
        if (auto* m = h->getMidi()) m->stepLength = juce::jlimit (1, 64, n);
}

int getStepLength()
{
    if (auto* h = Hub::getIfExists())
        if (auto* m = h->getMidi()) return m->stepLength.load();
    return 16;
}

juce::int64 currentFrame() noexcept
{
    auto* hub = Hub::getIfExists();
    return hub != nullptr ? hub->getBlockFrame() : 0;
}

namespace
{
    // MELVILLE's own clock, for when there's no reel: rolls at the interface's rate
    std::atomic<bool> clockRolling { false };
    std::atomic<juce::int64> clockPos { 0 };          // samples
    bool usingTape() { auto& t = transport(); return t.available.load() && t.reelLoaded.load(); }
}

void play()         { auto& t = transport(); if (usingTape()) { if (t.play) t.play(); } else clockRolling = true; }
void stop()
{
    auto& t = transport();
    if (usingTape()) { if (t.stop) t.stop(); return; }
    if (! clockRolling.exchange (false)) clockPos = 0;     // stop again: back to the start
}
void record()       { auto& t = transport(); if (usingTape() && t.record) t.record(); }
void returnToZero() { auto& t = transport(); if (usingTape()) { if (t.returnToZero) t.returnToZero(); } else clockPos = 0; }
void wind (int dir) { auto& t = transport(); if (usingTape() && t.wind) t.wind (dir); }
bool isRolling() noexcept { return usingTape() ? transport().rolling.load() : clockRolling.load(); }

void setTempo (double bpm, int bpb, double barOne)
{
    auto& t = transport();
    t.bpm = juce::jlimit (20.0, 400.0, bpm);
    t.beatsPerBar = juce::jlimit (1, 16, bpb);
    t.barOne = juce::jmax (0.0, barOne);
    ++t.tempoRev;
    if (usingTape() && t.writeTempo) t.writeTempo (t.bpm.load(), t.beatsPerBar.load(), t.barOne.load());
}

void reelTempo (double bpm, int bpb, double barOne)
{
    auto& t = transport();
    if (bpm > 0.0)
    {
        // the reel's own tempo map: it belongs to the song on it
        t.bpm = bpm; t.beatsPerBar = bpb; t.barOne = barOne;
        ++t.tempoRev;
    }
    else if (t.writeTempo)
        t.writeTempo (t.bpm.load(), t.beatsPerBar.load(), t.barOne.load());   // a blank reel gets MELVILLE's
}

void setTimecode (int rate, double start)
{
    auto& t = transport();
    t.tcRate = juce::jlimit (0, 4, rate);
    t.tcStart = juce::jmax (0.0, start);
    if (usingTape() && t.writeTimecode) t.writeTimecode (t.tcRate.load(), t.tcStart.load());
}

void reelTimecode (int rate, double start)
{
    auto& t = transport();
    t.tcRate = juce::jlimit (0, 4, rate);
    t.tcStart = juce::jmax (0.0, start);
}

void clockBlock (juce::int64 frame, int n, double sampleRate) noexcept
{
    auto& t = transport();
    if (t.available.load (std::memory_order_relaxed) && t.reelLoaded.load (std::memory_order_relaxed))
        return;                                         // the tape is the clock (Hawthorne publishes)
    const bool rolling = clockRolling.load (std::memory_order_relaxed);
    const auto pos = clockPos.load (std::memory_order_relaxed);
    t.tapePos.store ((double) pos, std::memory_order_relaxed);
    t.tapePerSample.store (rolling ? 1.0 : 0.0, std::memory_order_relaxed);
    t.flags.store ((rolling ? 1 : 0) | 4, std::memory_order_relaxed);   // (4: a clock to follow)
    t.publishedFrame.store (frame, std::memory_order_release);
    t.position.store (pos, std::memory_order_relaxed);
    t.rolling.store (rolling, std::memory_order_relaxed);
    t.recording.store (false, std::memory_order_relaxed);
    t.reelRate.store (sampleRate, std::memory_order_relaxed);
    if (rolling) clockPos.store (pos + n, std::memory_order_relaxed);
}

bool tapePositionAt (juce::int64 frame, double& tapePos, double& tapePerSample, int& flags) noexcept
{
    auto& t = transport();
    const auto at = t.publishedFrame.load (std::memory_order_acquire);
    if (at < 0) return false;
    tapePerSample = t.tapePerSample.load (std::memory_order_relaxed);
    tapePos = t.tapePos.load (std::memory_order_relaxed) + (double) (frame - at) * tapePerSample;
    flags = t.flags.load (std::memory_order_relaxed);
    return true;
}

void detach (juce::AudioDeviceManager& dm)
{
    if (auto* hub = Hub::getIfExists())
        for (auto& n : hub->getNodes())
            if (auto* p = dynamic_cast<PartNode*> (n.get()))
                if (p->manager == &dm) p->manager = nullptr;
    for (auto it = sessionOwners.begin(); it != sessionOwners.end();)
        if (it->second == &dm) { sessions.erase (it->first); it = sessionOwners.erase (it); }
        else ++it;
}

} // namespace melville
