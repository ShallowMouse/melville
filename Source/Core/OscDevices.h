#pragma once

// The surfaces MELVILLE talks to over OSC, and what each one drives: the HAWTHORNE transport,
// the TWAIN control room, or everything. A device is sent only its type's pages and may press
// only its type's keys, so a transport can't touch the monitoring and the monitoring can't hit
// record. Kept with MELVILLE's settings; the list is edited in the INTERFACE window.
//
// (The types and the list live here, away from the OSC machinery, so the tests can read them.)

#include <juce_core/juce_core.h>
#include <vector>

namespace melville
{

/** What a surface drives. */
enum DeviceType { typeHawthorne = 0, typeTwain, typeEverything, numDeviceTypes };

inline juce::String deviceTypeName (int t)
{
    switch (t)
    {
        case typeHawthorne: return "HAWTHORNE";
        case typeTwain:     return "TWAIN";
        default:            return "Everything";
    }
}

inline juce::String deviceTypeWhat (int t)
{
    switch (t)
    {
        case typeHawthorne: return "the deck: transport, markers, locates, varispeed";
        case typeTwain:     return "the control room: source, speakers, level, dim";
        default:            return "every page MELVILLE has";
    }
}

/** Does this type own that message? (/melville/... is everybody's: sync and hello.) */
inline bool typeWants (int type, const juce::String& address)
{
    if (type == typeEverything || address.startsWith ("/melville/")) return true;
    if (type == typeTwain) return address.startsWith ("/twain/");
    return address.startsWith ("/hawthorne/") || address.startsWith ("/transport/");
}

/** The type a surface looks like, from a message it sent. */
inline int typeOf (const juce::String& address)
{
    if (address.startsWith ("/twain/")) return typeTwain;
    if (address.startsWith ("/hawthorne/")) return typeHawthorne;
    return typeEverything;
}

/** One surface MELVILLE talks to. */
struct OscDevice
{
    int type = typeEverything;
    juce::String name, host;         // what you call it; its address on the network
    int replyPort = 0;               // 0: the shared reply port
    juce::String serial;             // if it announced itself
};

/** The OSC settings: the port, and the devices on the list. */
struct OscSettings
{
    bool enabled = true;
    int listenPort = 8100, replyPort = 9100;
    juce::String replyHost;                 // (before the device list: addresses to answer)
    std::vector<OscDevice> devices;

    juce::StringArray fixedHosts() const
    {
        auto a = juce::StringArray::fromTokens (replyHost, ",; ", "");
        a.trim();
        a.removeEmptyStrings();
        return a;
    }

    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto x = std::make_unique<juce::XmlElement> ("OSC");
        x->setAttribute ("enabled", enabled);
        x->setAttribute ("listen", listenPort);
        x->setAttribute ("reply", replyPort);
        x->setAttribute ("replyHost", replyHost);
        for (auto& d : devices)
        {
            auto* e = x->createNewChildElement ("DEVICE");
            e->setAttribute ("type", d.type);
            e->setAttribute ("name", d.name);
            e->setAttribute ("host", d.host);
            if (d.replyPort > 0) e->setAttribute ("replyPort", d.replyPort);
            if (d.serial.isNotEmpty()) e->setAttribute ("serial", d.serial);
        }
        return x;
    }

    static OscSettings fromXml (const juce::XmlElement* x)
    {
        OscSettings s;
        if (x == nullptr) return s;
        s.enabled = x->getBoolAttribute ("enabled", true);
        s.listenPort = juce::jlimit (1, 65535, x->getIntAttribute ("listen", 8100));
        s.replyPort = juce::jlimit (1, 65535, x->getIntAttribute ("reply", 9100));
        s.replyHost = x->getStringAttribute ("replyHost").trim();
        for (auto* e : x->getChildWithTagNameIterator ("DEVICE"))
        {
            OscDevice d;
            d.type = juce::jlimit (0, numDeviceTypes - 1, e->getIntAttribute ("type", typeEverything));
            d.name = e->getStringAttribute ("name").trim();
            d.host = e->getStringAttribute ("host").trim();
            d.replyPort = juce::jlimit (0, 65535, e->getIntAttribute ("replyPort", 0));
            d.serial = e->getStringAttribute ("serial").trim();
            if (d.host.isNotEmpty()) s.devices.push_back (d);
        }
        // (from before the device list: the addresses it answered become devices)
        if (s.devices.empty())
            for (auto& h : s.fixedHosts())
                s.devices.push_back ({ typeEverything, h, h, 0, {} });
        return s;
    }
};

} // namespace melville
