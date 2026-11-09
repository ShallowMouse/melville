#pragma once

// MELVILLE's MIDI patch bay: MIDI on the patchbay as an early-80s studio had it, a 5-pin
// cable carrying all sixteen channels from one OUT to one IN (and a mult for THRU).
//
//   sources:       Keyboards (every MIDI input merged), In: <each MIDI input>,
//                  THOREAU steps (each pad on its own channel), Clock (24 a beat, start / stop)
//   destinations:  ALCOTT slot 1 - 8, Out: <each MIDI output>
//
// It's a node like the others, so its jacks are patchbay channels, saved with the patch and
// cabled the same way; the patchbay only lets MIDI go to MIDI. Its audio is silence. The
// hub runs it first in the block (the keyboards, ready for everyone) and last (the outs).
// THOREAU writes its steps during its turn, MELVILLE's clock during its; ALCOTT reads its
// slots after THOREAU. MIDI from a part that runs after the one it's cabled to arrives a
// block later.

#include "Node.h"
#include <array>

namespace melville
{

class MidiNode : public Node, private juce::MidiInputCallback
{
public:
    static constexpr int numAlcottSlots = 8;

    MidiNode()
    {
        port.id = "midi";
        port.title = "MIDI";
        inputs = juce::MidiInput::getAvailableDevices();
        outputs = juce::MidiOutput::getAvailableDevices();
        port.sources.add ("Keyboards");
        for (auto& d : inputs) port.sources.add ("In: " + d.name);
        port.sources.add ("THOREAU steps");
        port.sources.add ("Clock");
        for (int k = 1; k <= numAlcottSlots; ++k) port.dests.add ("ALCOTT slot " + juce::String (k));
        for (auto& d : outputs) port.dests.add ("Out: " + d.name);
        buffers.resize ((size_t) port.sources.size());
        for (auto& b : buffers) b.ensureSize (2048);
        collectors.resize (inputs.size());
        for (auto& c : collectors) c = std::make_unique<juce::MidiMessageCollector>();
        for (auto& o : thoreauOff) o.fill (-1);
    }

    ~MidiNode() override { release(); }

    // the source jacks' numbers (within this port)
    int keyboards() const noexcept      { return 0; }
    int input (int i) const noexcept    { return 1 + i; }
    int thoreauSteps() const noexcept   { return 1 + inputs.size(); }
    int clock() const noexcept          { return 2 + inputs.size(); }
    int alcottSlot (int k) const noexcept { return k; }                    // (destination)
    int output (int i) const noexcept   { return numAlcottSlots + i; }    // (destination)

    juce::MidiBuffer& source (int i) noexcept { return buffers[(size_t) i]; }

    /** Set by the hub: where the cords are. */
    Patchbay* patchbay = nullptr;

    /** How long THOREAU's steps hold (it sends no note-offs): a 1/n note. */
    std::atomic<int> stepLength { 16 };
    std::function<double()> getBpm;          // set by the hub: MELVILLE's tempo (audio thread)

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        for (auto& c : collectors) c->reset (sampleRate);
    }

    /** Message thread (often): open the MIDI devices something is cabled to, and let go of the
        rest. (On Windows a MIDI device opens in one place at a time: one IRVING or THOREAU use
        stays theirs until it's cabled here.) */
    void openWhatsCabled()
    {
        if (patchbay == nullptr || portIndex < 0) return;
        const auto& pp = patchbay->getPorts()[(size_t) portIndex];
        std::vector<bool> inUsed ((size_t) inputs.size(), false), outUsed ((size_t) outputs.size(), false);
        for (auto& [d, c] : patchbay->getAllCords())
        {
            const int src = c.source - pp.firstSource, dst = d - pp.firstDest;
            if (src == keyboards()) std::fill (inUsed.begin(), inUsed.end(), true);
            else if (src >= input (0) && src < input (inputs.size())) inUsed[(size_t) (src - input (0))] = true;
            if (dst >= output (0) && dst < output (outputs.size())) outUsed[(size_t) (dst - output (0))] = true;
        }
        if (openIns.size() != (size_t) inputs.size()) openIns.resize ((size_t) inputs.size());
        for (int i = 0; i < inputs.size(); ++i)
        {
            auto& in = openIns[(size_t) i];
            if (inUsed[(size_t) i] && in == nullptr)
            {
                if ((in = juce::MidiInput::openDevice (inputs[i].identifier, this)) != nullptr)
                {
                    { const juce::ScopedLock sl (lookup); inputIndex[in.get()] = i; }
                    in->start();
                }
            }
            else if (! inUsed[(size_t) i] && in != nullptr)
            {
                in->stop();
                { const juce::ScopedLock sl (lookup); inputIndex.erase (in.get()); }
                in = nullptr;
            }
        }
        // the outputs: the audio thread sends on them, so a change waits for it to let go
        std::vector<juce::MidiOutput*> want ((size_t) outputs.size(), nullptr);
        bool changed = false;
        if (keptOuts.size() != (size_t) outputs.size()) keptOuts.resize ((size_t) outputs.size());
        for (int i = 0; i < outputs.size(); ++i)
        {
            auto& o = keptOuts[(size_t) i];
            if (outUsed[(size_t) i] && o == nullptr) { o = juce::MidiOutput::openDevice (outputs[i].identifier); changed = true; }
            if (! outUsed[(size_t) i] && o != nullptr) { retiredOuts.push_back (std::move (o)); changed = true; }
            want[(size_t) i] = o.get();
        }
        if (changed)
        {
            const juce::SpinLock::ScopedLockType sl (outLock);
            liveOuts = want;
        }
        else if (! retiredOuts.empty())
            retiredOuts.clear();        // (a pass after: the audio thread has let go of them)
    }

    void release() override
    {
        for (auto& in : openIns) if (in != nullptr) in->stop();
        openIns.clear();
        { const juce::SpinLock::ScopedLockType sl (outLock); liveOuts.clear(); }
        keptOuts.clear();
        retiredOuts.clear();
        const juce::ScopedLock sl (lookup);
        inputIndex.clear();
    }

    /** Start of the block: the keyboards' notes in, THOREAU's held steps let go. */
    void pull (float* const* sources, int n) noexcept override
    {
        for (int c = 0; c < port.sources.size(); ++c) juce::FloatVectorOperations::clear (sources[c], n);
        for (auto& b : buffers) b.clear();
        const int passed = frameN;          // (the block just gone: THOREAU's held steps count it down)
        frameN = n;
        for (int i = 0; i < inputs.size(); ++i)
        {
            auto& b = buffers[(size_t) input (i)];
            collectors[(size_t) i]->removeNextBlockOfMessages (b, n);
            buffers[(size_t) keyboards()].addEvents (b, 0, n, 0);
        }
        auto& steps = buffers[(size_t) thoreauSteps()];
        for (int ch = 0; ch < 16; ++ch)
            for (int note = 0; note < 128; ++note)
            {
                auto& o = thoreauOff[(size_t) ch][(size_t) note];
                if (o < 0) continue;
                o = juce::jmax (0, o - passed);
                if (o < n) { steps.addEvent (juce::MidiMessage::noteOff (ch + 1, note), o); o = -1; }
            }
    }

    /** THOREAU's turn (audio thread): a step played, `offset` samples into the block. */
    void postStep (int channel, int note, float velocity, int offset) noexcept
    {
        const int ch = juce::jlimit (1, 16, channel), nt = juce::jlimit (0, 127, note);
        const int at = juce::jlimit (0, juce::jmax (0, frameN - 1), offset);
        auto& steps = buffers[(size_t) thoreauSteps()];
        auto& o = thoreauOff[(size_t) ch - 1][(size_t) nt];
        if (o >= 0) steps.addEvent (juce::MidiMessage::noteOff (ch, nt), at);       // (a repeat: let the last go)
        steps.addEvent (juce::MidiMessage::noteOn (ch, nt, juce::jlimit (0.05f, 1.0f, velocity)), at);
        const double len = sr * 60.0 / juce::jmax (20.0, getBpm ? getBpm() : 120.0) * 4.0
                           / juce::jlimit (1, 64, stepLength.load (std::memory_order_relaxed)) * 0.9;
        o = at + juce::jmax (64, (int) len);
    }

    /** A destination's MIDI: everything cabled into it this block (audio thread). */
    void take (int dest, juce::MidiBuffer& into) const noexcept
    {
        if (patchbay == nullptr || portIndex < 0) return;
        const auto* m = patchbay->getMatrix();
        if (m == nullptr) return;
        const auto& pp = patchbay->getPorts()[(size_t) portIndex];
        const int flat = pp.firstDest + dest;
        if (! juce::isPositiveAndBelow (flat, (int) m->into.size())) return;
        for (auto& c : m->into[(size_t) flat])
        {
            const int s = c.source - pp.firstSource;
            if (juce::isPositiveAndBelow (s, (int) buffers.size()))
                into.addEvents (buffers[(size_t) s], 0, frameN, 0);
        }
    }

    /** End of the block: the MIDI outs. */
    void push (const float* const*, int) noexcept override
    {
        const juce::SpinLock::ScopedTryLockType sl (outLock);
        if (! sl.isLocked()) return;
        for (int i = 0; i < (int) liveOuts.size(); ++i)
        {
            if (liveOuts[(size_t) i] == nullptr) continue;
            outBlock.clear();
            take (output (i), outBlock);
            if (! outBlock.isEmpty()) liveOuts[(size_t) i]->sendBlockOfMessagesNow (outBlock);
        }
    }

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m) override
    {
        if (m.isActiveSense()) return;
        const juce::ScopedLock sl (lookup);
        const auto it = inputIndex.find (source);
        if (it != inputIndex.end()) collectors[(size_t) it->second]->addMessageToQueue (m);
    }

    juce::Array<juce::MidiDeviceInfo> inputs, outputs;
    std::vector<std::unique_ptr<juce::MidiInput>> openIns;
    std::vector<std::unique_ptr<juce::MidiOutput>> keptOuts, retiredOuts;
    std::vector<juce::MidiOutput*> liveOuts;                // (what the audio thread sends on)
    juce::SpinLock outLock;
    juce::CriticalSection lookup;
    std::map<juce::MidiInput*, int> inputIndex;
    std::vector<std::unique_ptr<juce::MidiMessageCollector>> collectors;
    std::vector<juce::MidiBuffer> buffers;
    juce::MidiBuffer outBlock;
    std::array<std::array<int, 128>, 16> thoreauOff {};
    double sr = 48000.0;
    int frameN = 0;
};

} // namespace melville
