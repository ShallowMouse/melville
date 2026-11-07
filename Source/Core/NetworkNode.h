#pragma once

// Hawthorne Link in MELVILLE: up to four senders on the network (THOREAU, PEQUOD Host or
// another PC), each a block of channels on the NETWORK patch panel, cabled wherever you
// like. The receiver keeps each stream locked (or drift-corrected) to MELVILLE's clock,
// as it did inside Hawthorne.

#include "Node.h"
#include "NetReceiver.h"      // Hawthorne: Source/Network
#include <juce_data_structures/juce_data_structures.h>

namespace melville
{

class NetworkNode : public Node
{
public:
    NetworkNode();
    ~NetworkNode() override;

    hawthorne::NetReceiver& getReceiver() noexcept { return receiver; }

    /** Start listening, with the settings saved before (MELVILLE's, else Hawthorne's). */
    void start (juce::PropertiesFile& settings, juce::PropertiesFile* hawthorneSettings);
    void saveSettings (juce::PropertiesFile&) const;

    void prepare (double sampleRate, int maxBlock) override;
    void pull (float* const* sources, int n) noexcept override;

    static constexpr int slots = hawthorne::NetReceiver::maxSlots;
    static constexpr int channelsPerSlot = hawthorne::net::maxChannels;
    static juce::String channelName (int slot, int ch) { return "Link " + juce::String (slot + 1) + " ch " + juce::String (ch + 1); }

private:
    hawthorne::NetReceiver receiver;
};

} // namespace melville
