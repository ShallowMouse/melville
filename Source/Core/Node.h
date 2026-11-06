#pragma once

// Something the hub runs each block: a part (an app's engine, through its PartDevice),
// a bridge (Hawthorne Route or the EMERSON Link, played from MELVILLE's side), or the
// interface. A node has a port on the patchbay: its sources (what it puts out) and its
// destinations (what it takes in).
//
// A node runs in one or two steps. A part takes its inputs and makes its outputs in one
// go. A bridge is split: PULL publishes what the app on the near side wrote (its sources),
// PUSH hands the patched destinations to the app on the far side, so the two halves can
// sit either side of the part between them in the block.

#include <juce_audio_devices/juce_audio_devices.h>
#include "Patchbay.h"

namespace melville
{

class Node
{
public:
    virtual ~Node() = default;

    Port port;            // id, title, channel names (set before the hub adds it)
    int portIndex = -1;   // in the patchbay (set by the hub)

    virtual void prepare (double sampleRate, int maxBlock) { juce::ignoreUnused (sampleRate, maxBlock); }
    virtual void release() {}

    /** PULL: write this block's sources (port.sources.size() channels). */
    virtual void pull (float* const* sources, int n) noexcept { juce::ignoreUnused (sources, n); }
    /** PUSH: take this block's destinations (port.dests.size() channels). */
    virtual void push (const float* const* dests, int n) noexcept { juce::ignoreUnused (dests, n); }
    /** A part: take destinations and make sources in one go (the default: push then pull). */
    virtual void process (const float* const* dests, float* const* sources, int n) noexcept
    {
        push (dests, n);
        pull (sources, n);
    }
};

/** One entry of the hub's running order. */
struct Step
{
    enum Kind { pull, push, process } kind;
    Node* node;
};

class PartDevice;

/** An app running inside MELVILLE. Its engine's callback is whatever its PartDevice was started with. */
class PartNode : public Node
{
public:
    PartNode (juce::String id, juce::String title, juce::StringArray ins, juce::StringArray outs)
    {
        port.id = std::move (id);
        port.title = std::move (title);
        port.dests = std::move (ins);       // the part's inputs are patchbay destinations
        port.sources = std::move (outs);    // its outputs are sources
        deviceName = port.title;
        showAll();
    }

    /** The name the part's device has. EMERSON's and WHITMAN's include "Hawthorne Route",
        so their code treats it as the Route (and turns every channel on). */
    juce::String deviceName;

    /** The part's own device (it opens it through its AudioDeviceManager); the latest
        one opened. Set and cleared by the hub under its lock. */
    std::atomic<PartDevice*> device { nullptr };

    void process (const float* const* dests, float* const* sources, int n) noexcept override;

    /** The channels the part's device shows, as port channel numbers (default: all). EMERSON
        and WHITMAN show only the reel's tracks, as Hawthorne Route does. Message thread. */
    std::vector<int> visibleIns, visibleOuts;
    void showAll()
    {
        visibleIns.resize ((size_t) port.dests.size());
        visibleOuts.resize ((size_t) port.sources.size());
        for (size_t i = 0; i < visibleIns.size(); ++i)  visibleIns[i] = (int) i;
        for (size_t i = 0; i < visibleOuts.size(); ++i) visibleOuts[i] = (int) i;
    }

    /** The part's AudioDeviceManager (set by melville::attach), so the hub can reopen its
        device when the channels it shows change. */
    juce::AudioDeviceManager* manager = nullptr;
};

} // namespace melville
