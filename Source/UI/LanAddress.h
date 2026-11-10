#pragma once

#include <JuceHeader.h>

namespace melville
{

/** This computer's addresses on the local network, best first: private LAN addresses
    (192.168.x, 10.x, 172.16-31.x), then any other real one. Leaves out loopback and the
    169.254.x addresses an adapter gives itself when it isn't connected to anything. */
inline juce::StringArray lanAddresses()
{
    juce::StringArray lan, other;
    for (auto& a : juce::IPAddress::getAllAddresses (false))
    {
        const auto s = a.toString();
        if (a.isNull() || s.startsWith ("127.") || s.startsWith ("169.254.") || s == "0.0.0.0") continue;
        const int b = s.fromFirstOccurrenceOf (".", false, false).upToFirstOccurrenceOf (".", false, false).getIntValue();
        const bool priv = s.startsWith ("192.168.") || s.startsWith ("10.") || (s.startsWith ("172.") && b >= 16 && b <= 31);
        (priv ? lan : other).addIfNotAlreadyThere (s);
    }
    lan.addArray (other);
    return lan;
}

/** The best one (what to type into a phone or a tablet), or the system's pick if there's none. */
inline juce::String lanAddress()
{
    const auto all = lanAddresses();
    return all.isEmpty() ? juce::IPAddress::getLocalAddress().toString() : all[0];
}

} // namespace melville
