#include "NetworkNode.h"

namespace melville
{

NetworkNode::NetworkNode()
{
    port.id = "network";
    port.title = "NETWORK";
    for (int s = 0; s < slots; ++s)
        for (int c = 0; c < channelsPerSlot; ++c)
            port.sources.add (channelName (s, c));
    for (int c = 0; c < maxSend; ++c)
        port.dests.add (sendName (c));
    receiver.ignoreSender (sender.getSenderId());       // (MELVILLE doesn't list itself)
}

NetworkNode::~NetworkNode()
{
    sender.stop();
    receiver.stop();
}

bool NetworkNode::setSending (bool on, juce::String& error)
{
    if (on && ! sending.load())
    {
        if (! sender.start (sender.getName(), error)) return false;
    }
    else if (! on && sending.load())
        sender.stop();
    sending = on;
    return true;
}

void NetworkNode::start (juce::PropertiesFile& settings, juce::PropertiesFile* hawthorneSettings)
{
    // the first time, carry over what Hawthorne had
    auto* from = settings.containsKey ("netSlots") || hawthorneSettings == nullptr ? &settings : hawthorneSettings;
    receiver.setTargetLatencyMs (from->getDoubleValue ("netMinBufferMs", 3.0));
    receiver.setManualAddresses (juce::StringArray::fromLines (from->getValue ("netManual")));
    const auto names = juce::StringArray::fromLines (from->getValue ("netSlots"));
    for (int slot = 0; slot < juce::jmin (names.size(), slots); ++slot)
        if (names[slot].isNotEmpty())
            receiver.connect (slot, names[slot]);

    juce::String error;
    receiver.start (error);

    // the sender: as it was left
    sender.setName (settings.getValue ("sendName", "MELVILLE - " + juce::SystemStats::getComputerName()));
    sender.setRedundancy (settings.getBoolValue ("sendRedundancy", true));
    setSendChannels (settings.getIntValue ("sendChannels", 8));
    if (settings.getBoolValue ("sendOn", false)) setSending (true, error);
}

void NetworkNode::saveSettings (juce::PropertiesFile& s) const
{
    auto& rx = const_cast<hawthorne::NetReceiver&> (receiver);
    s.setValue ("netMinBufferMs", rx.getTargetLatencyMs());
    s.setValue ("netManual", rx.getManualAddresses().joinIntoString ("\n"));
    juce::StringArray names;
    for (int slot = 0; slot < slots; ++slot)
        names.add (rx.getSlotName (slot));
    s.setValue ("netSlots", names.joinIntoString ("\n"));
    s.setValue ("sendOn", sending.load());
    s.setValue ("sendName", sender.getName());
    s.setValue ("sendRedundancy", const_cast<hawthorne::NetSender&> (sender).getRedundancy());
    s.setValue ("sendChannels", sendChannels.load());
}

void NetworkNode::prepare (double sampleRate, int)
{
    receiver.prepare (sampleRate);
    rate = sampleRate;
    preparedChannels = sendChannels.load();
    sender.prepare (sampleRate, preparedChannels);
}

void NetworkNode::push (const float* const* dests, int n) noexcept
{
    if (! sending.load (std::memory_order_relaxed)) return;
    // a new number of channels starts a new session (rare: when it's changed in the LINK window)
    if (const int want = sendChannels.load (std::memory_order_relaxed); want != preparedChannels)
    {
        preparedChannels = want;
        sender.prepare (rate, want);
    }
    sender.push (dests, preparedChannels, n);
}

void NetworkNode::pull (float* const* sources, int n) noexcept
{
    // the receiver works in chunks of at most maxBlock
    for (int done = 0; done < n;)
    {
        const int chunk = juce::jmin (n - done, hawthorne::NetReceiver::maxBlock);
        receiver.process (chunk);
        for (int s = 0; s < slots; ++s)
            for (int c = 0; c < channelsPerSlot; ++c)
            {
                float* dst = sources[s * channelsPerSlot + c] + done;
                if (const float* src = receiver.getChannel (s, c)) juce::FloatVectorOperations::copy (dst, src, chunk);
                else juce::FloatVectorOperations::clear (dst, chunk);
            }
        done += chunk;
    }
}

} // namespace melville
