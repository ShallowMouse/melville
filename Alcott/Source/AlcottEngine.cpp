#include "AlcottEngine.h"
#include <MelvilleHosted.h>

namespace alcott
{

//==============================================================================
/** MELVILLE's tempo and transport, for the instruments' arpeggiators and synced LFOs. */
class Engine::PlayHead : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        auto& t = melville::transport();
        PositionInfo p;
        const double bpm = t.bpm.load (std::memory_order_relaxed);
        p.setBpm (bpm);
        p.setTimeSignature (TimeSignature { t.beatsPerBar.load (std::memory_order_relaxed), 4 });
        const bool rolling = t.rolling.load (std::memory_order_relaxed);
        p.setIsPlaying (rolling);
        const double rate = juce::jmax (1000.0, t.reelRate.load (std::memory_order_relaxed));
        p.setPpqPosition ((t.tapePos.load (std::memory_order_relaxed) - t.barOne.load (std::memory_order_relaxed)) / rate * bpm / 60.0);
        return p;
    }
};

//==============================================================================
Engine::Engine() : playHead (std::make_unique<PlayHead>())
{
    for (auto& v : thoreauOff) v.fill (-1);
    work.setSize (2, 4096);
    mix.setSize (2, 4096);
    keyBlock.ensureSize (4096);
    for (auto& m : slotMidi) m.ensureSize (2048);
    keys.reset (sampleRate);
}

Engine::~Engine()
{
    const juce::ScopedLock sl (chainLock);
    for (auto& s : slots) { s.instrument.reset(); s.effects.clear(); }
}

std::vector<const PequodNative::Info*> Engine::instruments()
{
    std::vector<const PequodNative::Info*> v;
    for (auto& i : PequodNative::all()) if (i.instrument) v.push_back (&i);
    std::sort (v.begin(), v.end(), [] (auto* a, auto* b) { return a->name.compareNatural (b->name) < 0; });
    return v;
}

std::vector<const PequodNative::Info*> Engine::effects()
{
    std::vector<const PequodNative::Info*> v;
    for (auto& i : PequodNative::all()) if (! i.instrument) v.push_back (&i);
    std::sort (v.begin(), v.end(), [] (auto* a, auto* b) { return a->name.compareNatural (b->name) < 0; });
    return v;
}

//==============================================================================
std::unique_ptr<Engine::Unit> Engine::makeUnit (const juce::String& id, juce::String& error)
{
    const auto* info = PequodNative::find (id);
    if (info == nullptr) { error = "There's no PEQUOD plugin " + id + " in this build."; return nullptr; }
    auto u = std::make_unique<Unit>();
    u->id = id;
    u->name = info->name;
    u->uid = nextUid++;
    auto inst = std::make_unique<PequodNative::Instance> (*info);
    inst->setPlayHead (playHead.get());
    inst->enableAllBuses();
    inst->setRateAndBufferSizeDetails (sampleRate, blockSize);
    inst->prepareToPlay (sampleRate, blockSize);
    u->plugin = std::move (inst);
    return u;
}

bool Engine::setInstrument (int s, const juce::String& id)
{
    std::unique_ptr<Unit> u;
    if (id.isNotEmpty())
    {
        juce::String err;
        u = makeUnit (id, err);
        if (u == nullptr) return false;
    }
    {
        const juce::ScopedLock sl (chainLock);
        std::swap (slot (s).instrument, u);
    }
    u.reset();                        // the old one goes here, off the audio thread
    if (onChanged) onChanged();
    return true;
}

bool Engine::addEffect (int s, const juce::String& id, int at)
{
    juce::String err;
    auto u = makeUnit (id, err);
    if (u == nullptr) return false;
    {
        const juce::ScopedLock sl (chainLock);
        auto& fx = slot (s).effects;
        at = at < 0 ? (int) fx.size() : juce::jlimit (0, (int) fx.size(), at);
        fx.insert (fx.begin() + at, std::move (u));
    }
    if (onChanged) onChanged();
    return true;
}

void Engine::removeEffect (int s, int uid)
{
    std::unique_ptr<Unit> gone;
    {
        const juce::ScopedLock sl (chainLock);
        auto& fx = slot (s).effects;
        for (auto it = fx.begin(); it != fx.end(); ++it)
            if ((*it)->uid == uid) { gone = std::move (*it); fx.erase (it); break; }
    }
    gone.reset();
    if (onChanged) onChanged();
}

void Engine::moveEffect (int s, int uid, int delta)
{
    {
        const juce::ScopedLock sl (chainLock);
        auto& fx = slot (s).effects;
        for (int i = 0; i < (int) fx.size(); ++i)
            if (fx[(size_t) i]->uid == uid)
            {
                const int j = juce::jlimit (0, (int) fx.size() - 1, i + delta);
                std::swap (fx[(size_t) i], fx[(size_t) j]);
                break;
            }
    }
    if (onChanged) onChanged();
}

void Engine::clearSlot (int s)
{
    std::unique_ptr<Unit> inst;
    std::vector<std::unique_ptr<Unit>> fx;
    {
        const juce::ScopedLock sl (chainLock);
        std::swap (slot (s).instrument, inst);
        std::swap (slot (s).effects, fx);
        slot (s).name = {};
    }
    inst.reset();
    fx.clear();
    if (onChanged) onChanged();
}

//==============================================================================
static juce::String stateOf (juce::AudioPluginInstance& p)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    return mb.toBase64Encoding();
}

static void restoreState (juce::AudioPluginInstance& p, const juce::String& b64)
{
    juce::MemoryBlock mb;
    if (b64.isNotEmpty() && mb.fromBase64Encoding (b64))
        p.setStateInformation (mb.getData(), (int) mb.getSize());
}

std::unique_ptr<juce::XmlElement> Engine::toXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("ALCOTT");
    x->setAttribute ("thoreauLength", thoreauLength.load());
    const juce::ScopedLock sl (chainLock);
    for (int i = 0; i < numSlots; ++i)
    {
        const auto& s = slots[(size_t) i];
        auto* e = x->createNewChildElement ("SLOT");
        e->setAttribute ("index", i);
        e->setAttribute ("name", s.name);
        e->setAttribute ("source", s.source.load());
        e->setAttribute ("channel", s.channel.load());
        e->setAttribute ("level", s.levelDb.load());
        e->setAttribute ("mute", s.mute.load());
        auto unit = [&] (const char* tag, const Unit& u)
        {
            auto* ue = e->createNewChildElement (tag);
            ue->setAttribute ("id", u.id);
            ue->setAttribute ("bypassed", u.bypassed.load());
            ue->addTextElement (stateOf (*u.plugin));
        };
        if (s.instrument != nullptr) unit ("INSTRUMENT", *s.instrument);
        for (auto& f : s.effects) unit ("EFFECT", *f);
    }
    return x;
}

void Engine::fromXml (const juce::XmlElement& x)
{
    if (! x.hasTagName ("ALCOTT")) return;
    thoreauLength = x.getIntAttribute ("thoreauLength", 16);
    for (int i = 0; i < numSlots; ++i)
    {
        const juce::XmlElement* e = nullptr;
        for (auto* c : x.getChildWithTagNameIterator ("SLOT")) if (c->getIntAttribute ("index", -1) == i) e = c;

        // build the new chain off the audio thread, then swap it in
        std::unique_ptr<Unit> inst;
        std::vector<std::unique_ptr<Unit>> fx;
        if (e != nullptr)
            for (auto* ue : e->getChildIterator())
            {
                juce::String err;
                auto u = makeUnit (ue->getStringAttribute ("id"), err);
                if (u == nullptr) continue;
                restoreState (*u->plugin, ue->getAllSubText().trim());
                u->bypassed = ue->getBoolAttribute ("bypassed", false);
                if (ue->hasTagName ("INSTRUMENT")) inst = std::move (u);
                else fx.push_back (std::move (u));
            }
        auto& s = slots[(size_t) i];
        {
            const juce::ScopedLock sl (chainLock);
            std::swap (s.instrument, inst);
            std::swap (s.effects, fx);
            s.name = e != nullptr ? e->getStringAttribute ("name") : juce::String();
        }
        s.source = e != nullptr ? e->getIntAttribute ("source", both) : (int) both;
        s.channel = e != nullptr ? e->getIntAttribute ("channel", 0) : 0;
        s.levelDb = e != nullptr ? (float) e->getDoubleAttribute ("level", 0.0) : 0.0f;
        s.mute = e != nullptr && e->getBoolAttribute ("mute", false);
    }
}

//==============================================================================
void Engine::playNote (int s, int note, float velocity)
{
    const juce::SpinLock::ScopedLockType sl (directWrite);
    const auto w = directFifo.write (1);
    if (w.blockSize1 > 0)      direct[(size_t) w.startIndex1] = { s, note, velocity };
    else if (w.blockSize2 > 0) direct[(size_t) w.startIndex2] = { s, note, velocity };
}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    keys.addMessageToQueue (m);
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    const juce::ScopedLock sl (chainLock);
    sampleRate = d->getCurrentSampleRate();
    blockSize = juce::jmax (64, d->getCurrentBufferSizeSamples());
    work.setSize (2, juce::jmax (4096, blockSize));
    mix.setSize (2, juce::jmax (4096, blockSize));
    keys.reset (sampleRate);
    for (auto& s : slots)
    {
        if (s.instrument != nullptr) { s.instrument->plugin->setRateAndBufferSizeDetails (sampleRate, blockSize); s.instrument->plugin->prepareToPlay (sampleRate, blockSize); }
        for (auto& f : s.effects)    { f->plugin->setRateAndBufferSizeDetails (sampleRate, blockSize); f->plugin->prepareToPlay (sampleRate, blockSize); }
    }
}

void Engine::prepareForTest (double rate, int block)
{
    const juce::ScopedLock sl (chainLock);
    sampleRate = rate;
    blockSize = block;
    keys.reset (sampleRate);
}

void Engine::audioDeviceStopped() {}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut,
                                               int n, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    for (int done = 0; done < n;)
    {
        const int len = juce::jmin (n - done, work.getNumSamples());
        std::array<float*, numOutputs> o {};
        for (int c = 0; c < juce::jmin (numOut, numOutputs); ++c) o[(size_t) c] = out[c] != nullptr ? out[c] + done : nullptr;
        process (o.data(), juce::jmin (numOut, numOutputs), len);
        done += len;
    }
}

void Engine::process (float* const* out, int numOut, int n) noexcept
{
    // the keyboards' notes for this block, THOREAU's, and the window's
    keyBlock.clear();
    keys.removeNextBlockOfMessages (keyBlock, n);
    std::array<melville::NoteEvent, 256> fromThoreau;
    const int numThoreau = melville::takeNotes (fromThoreau.data(), (int) fromThoreau.size());
    const auto frame = melville::currentFrame();

    const juce::ScopedTryLock sl (chainLock);
    if (! sl.isLocked()) return;          // (a slot's chain is being changed: a block of silence)

    for (auto& m : slotMidi) m.clear();
    {
        const auto r = directFifo.read (directFifo.getNumReady());
        auto take = [&] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& d = direct[(size_t) (start + i)];
                if (! juce::isPositiveAndBelow (d.slot, numSlots)) continue;
                slotMidi[(size_t) d.slot].addEvent (d.velocity > 0.0f ? juce::MidiMessage::noteOn (1, d.note, d.velocity)
                                                                      : juce::MidiMessage::noteOff (1, d.note), 0);
            }
        };
        take (r.startIndex1, r.blockSize1);
        take (r.startIndex2, r.blockSize2);
    }

    // THOREAU's steps hold for a sixteenth (it sends no note-offs)
    const double bpm = juce::jmax (20.0, melville::transport().bpm.load (std::memory_order_relaxed));
    const int gate = juce::jmax (64, (int) (sampleRate * 60.0 / bpm * 4.0 / juce::jlimit (1, 64, thoreauLength.load (std::memory_order_relaxed)) * 0.9));

    juce::FloatVectorOperations::clear (mix.getWritePointer (0), n);
    juce::FloatVectorOperations::clear (mix.getWritePointer (1), n);

    for (int i = 0; i < numSlots; ++i)
    {
        auto& s = slots[(size_t) i];
        auto& midi = slotMidi[(size_t) i];
        const int src = s.source.load (std::memory_order_relaxed), ch = s.channel.load (std::memory_order_relaxed);
        bool any = ! midi.isEmpty();

        if (src == keyboard || src == both)
            for (const auto meta : keyBlock)
            {
                const auto m = meta.getMessage();
                if (ch == 0 || m.getChannel() == ch || m.getChannel() == 0) { midi.addEvent (m, meta.samplePosition); any = true; }
            }

        // THOREAU's held notes letting go in this block
        auto& offs = thoreauOff[(size_t) i];
        for (int note = 0; note < 128; ++note)
            if (offs[(size_t) note] >= 0)
            {
                if (offs[(size_t) note] < n) { midi.addEvent (juce::MidiMessage::noteOff (1, note), offs[(size_t) note]); offs[(size_t) note] = -1; }
                else offs[(size_t) note] -= n;
            }
        if (src == thoreau || src == both)
            for (int k = 0; k < numThoreau; ++k)
            {
                const auto& e = fromThoreau[(size_t) k];
                if (ch != 0 && e.channel != ch) continue;
                if (e.frame < frame - 4 * (juce::int64) n) continue;        // (stale: from before ALCOTT was listening)
                const int at = e.frame == frame ? juce::jlimit (0, n - 1, e.offset) : 0;
                if (offs[(size_t) e.note] >= 0) midi.addEvent (juce::MidiMessage::noteOff (1, e.note), at);   // (a repeat: let the last one go)
                midi.addEvent (juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.05f, 1.0f, e.velocity)), at);
                offs[(size_t) e.note] = at + gate;
                any = true;
            }
        if (any) s.noteLamp.store (1.0f, std::memory_order_relaxed);

        if (s.instrument == nullptr) { midi.clear(); continue; }

        juce::AudioBuffer<float> buf (work.getArrayOfWritePointers(), 2, n);
        buf.clear();
        s.instrument->plugin->processBlock (buf, midi);
        if (s.instrument->bypassed.load (std::memory_order_relaxed)) buf.clear();
        juce::MidiBuffer noMidi;
        for (auto& f : s.effects)
            if (! f->bypassed.load (std::memory_order_relaxed))
                f->plugin->processBlock (buf, noMidi);

        const float g = s.mute.load (std::memory_order_relaxed) ? 0.0f : juce::Decibels::decibelsToGain (s.levelDb.load (std::memory_order_relaxed), -80.0f);
        float pk = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            auto* d = buf.getWritePointer (c);
            juce::FloatVectorOperations::multiply (d, g, n);
            pk = juce::jmax (pk, juce::FloatVectorOperations::findMaximum (d, n), -juce::FloatVectorOperations::findMinimum (d, n));
            if (2 * i + c < numOut && out[2 * i + c] != nullptr) juce::FloatVectorOperations::copy (out[2 * i + c], d, n);
            juce::FloatVectorOperations::add (mix.getWritePointer (c), d, n);
        }
        if (pk > s.peak.load (std::memory_order_relaxed)) s.peak.store (pk, std::memory_order_relaxed);
    }

    for (int c = 0; c < 2; ++c)
        if (2 * numSlots + c < numOut && out[2 * numSlots + c] != nullptr)
            juce::FloatVectorOperations::copy (out[2 * numSlots + c], mix.getReadPointer (c), n);
}

} // namespace alcott
