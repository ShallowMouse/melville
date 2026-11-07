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
}

NetworkNode::~NetworkNode()
{
    receiver.stop();
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
}

void NetworkNode::prepare (double sampleRate, int)
{
    receiver.prepare (sampleRate);
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
