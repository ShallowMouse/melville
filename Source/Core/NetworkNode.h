#pragma once

// Hawthorne Link in MELVILLE: up to four senders on the network (THOREAU, PEQUOD Host or
// another PC), each a block of channels on the NETWORK patch panel, cabled wherever you
// like. The receiver keeps each stream locked (or drift-corrected) to MELVILLE's clock,
// as it did inside Hawthorne.
//
// And MELVILLE's own sender: up to 16 jacks on the patch panel (LINK SEND), streamed to
// MELVILLE (or MELVILLE Link) on another PC, where they arrive as a block of channels.

#include "Node.h"
#include "NetReceiver.h"      // Hawthorne: Source/Network
#include "NetSender.h"
#include <juce_data_structures/juce_data_structures.h>

namespace melville
{

class NetworkNode : public Node
{
public:
    NetworkNode();
    ~NetworkNode() override;

    hawthorne::NetReceiver& getReceiver() noexcept { return receiver; }
    hawthorne::NetSender& getSender() noexcept { return sender; }

    // --- the sender (message thread)
    static constexpr int maxSend = 16;
    bool setSending (bool on, juce::String& error);
    bool isSending() const noexcept { return sending.load(); }
    void setSendChannels (int n) noexcept { sendChannels = juce::jlimit (1, maxSend, n); }
    int getSendChannels() const noexcept { return sendChannels.load(); }
    static juce::String sendName (int ch) { return "Send " + juce::String (ch + 1); }

    /** Start listening, with the settings saved before (MELVILLE's, else Hawthorne's). */
    void start (juce::PropertiesFile& settings, juce::PropertiesFile* hawthorneSettings);
    void saveSettings (juce::PropertiesFile&) const;

    void prepare (double sampleRate, int maxBlock) override;
    void pull (float* const* sources, int n) noexcept override;
    void push (const float* const* dests, int n) noexcept override;

    static constexpr int slots = hawthorne::NetReceiver::maxSlots;
    static constexpr int channelsPerSlot = hawthorne::net::maxChannels;
    static juce::String channelName (int slot, int ch) { return "Link " + juce::String (slot + 1) + " ch " + juce::String (ch + 1); }

private:
    hawthorne::NetReceiver receiver;
    hawthorne::NetSender sender;
    std::atomic<bool> sending { false };
    std::atomic<int> sendChannels { 8 };
    int preparedChannels = 0;                 // (audio thread)
    double rate = 48000.0;
};

} // namespace melville
