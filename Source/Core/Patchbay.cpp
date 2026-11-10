#include "Patchbay.h"

namespace melville
{

Patchbay::~Patchbay()
{
    delete live.exchange (nullptr);
    for (auto& r : retired) delete r.m;
}

int Patchbay::addPort (Port p)
{
    p.firstSource = numSources;
    p.firstDest = numDests;
    numSources += p.sources.size();
    numDests += p.dests.size();
    ports.push_back (std::move (p));
    edit.into.resize ((size_t) numDests);
    normals.resize ((size_t) numDests);
    publish();
    return (int) ports.size() - 1;
}

const Port* Patchbay::findPort (const juce::String& id) const
{
    for (auto& p : ports)
        if (p.id == id) return &p;
    return nullptr;
}

juce::String Patchbay::sourceName (int s) const
{
    for (auto& p : ports)
        if (s >= p.firstSource && s < p.firstSource + p.sources.size())
            return p.title + " / " + p.sources[s - p.firstSource];
    return {};
}

juce::String Patchbay::destName (int d) const
{
    for (auto& p : ports)
        if (d >= p.firstDest && d < p.firstDest + p.dests.size())
            return p.title + " / " + p.dests[d - p.firstDest];
    return {};
}

int Patchbay::findSource (const juce::String& portId, const juce::String& channel) const
{
    if (auto* p = findPort (portId))
        if (const int i = p->sources.indexOf (channel); i >= 0) return p->firstSource + i;
    return -1;
}

int Patchbay::findDest (const juce::String& portId, const juce::String& channel) const
{
    if (auto* p = findPort (portId))
        if (const int i = p->dests.indexOf (channel); i >= 0) return p->firstDest + i;
    return -1;
}

bool Patchbay::isConnected (int source, int dest) const
{
    if (! juce::isPositiveAndBelow (dest, numDests)) return false;
    for (auto& c : edit.into[(size_t) dest])
        if (c.source == source) return true;
    return false;
}

bool Patchbay::isMidiSource (int s) const
{
    for (auto& p : ports) if (s >= p.firstSource && s < p.firstSource + p.sources.size()) return p.id == "midi";
    return false;
}

bool Patchbay::isMidiDest (int d) const
{
    for (auto& p : ports) if (d >= p.firstDest && d < p.firstDest + p.dests.size()) return p.id == "midi";
    return false;
}

void Patchbay::connect (int source, int dest, float gain)
{
    if (! juce::isPositiveAndBelow (source, numSources) || ! juce::isPositiveAndBelow (dest, numDests)) return;
    if (isMidiSource (source) != isMidiDest (dest)) return;       // (MIDI only to MIDI, audio only to audio)
    plug (source, dest, gain);
    if (! wiringNormals) publish();
}

void Patchbay::plug (int source, int dest, float gain)
{
    if (wiringNormals) { normals[(size_t) dest] = { source, false }; return; }
    auto& cords = edit.into[(size_t) dest];
    for (auto& c : cords)
        if (c.source == source) { c.gain = gain; return; }
    cords.push_back ({ source, gain });
}

void Patchbay::disconnect (int source, int dest)
{
    if (! juce::isPositiveAndBelow (dest, numDests)) return;
    auto& cords = edit.into[(size_t) dest];
    cords.erase (std::remove_if (cords.begin(), cords.end(), [source] (const Cord& c) { return c.source == source; }), cords.end());
    publish();
}

void Patchbay::clearDest (int dest)
{
    if (! juce::isPositiveAndBelow (dest, numDests)) return;
    edit.into[(size_t) dest].clear();
    publish();
}

void Patchbay::clearAll()
{
    for (auto& c : edit.into) c.clear();
    publish();
}

int Patchbay::connectByName (const juce::String& fromPort, const juce::String& toPort)
{
    auto* a = findPort (fromPort);
    auto* b = findPort (toPort);
    if (a == nullptr || b == nullptr) return 0;
    if ((a->id == "midi") != (b->id == "midi")) return 0;          // (MIDI only to MIDI)
    int made = 0;
    for (int i = 0; i < a->sources.size(); ++i)
        if (const int j = b->dests.indexOf (a->sources[i]); j >= 0)
        {
            plug (a->firstSource + i, b->firstDest + j, 1.0f);
            ++made;
        }
    if (! wiringNormals) publish();
    return made;
}

int Patchbay::connectInOrder (const juce::String& fromPort, int fromOffset, const juce::String& toPort, int toOffset, int n)
{
    auto* a = findPort (fromPort);
    auto* b = findPort (toPort);
    if (a == nullptr || b == nullptr) return 0;
    if ((a->id == "midi") != (b->id == "midi")) return 0;          // (MIDI only to MIDI)
    n = juce::jmin (n, a->sources.size() - fromOffset, b->dests.size() - toOffset);
    for (int k = 0; k < n; ++k)
        plug (a->firstSource + fromOffset + k, b->firstDest + toOffset + k, 1.0f);
    if (! wiringNormals) publish();
    return juce::jmax (0, n);
}

std::vector<std::pair<int, Cord>> Patchbay::getAllCords() const
{
    std::vector<std::pair<int, Cord>> all;
    for (int d = 0; d < numDests; ++d)
        for (auto& c : edit.into[(size_t) d])
            all.push_back ({ d, c });
    return all;
}

Matrix Patchbay::live_() const
{
    Matrix m (edit);
    std::vector<bool> plugged ((size_t) numSources, false);
    for (auto& cords : edit.into)
        for (auto& c : cords) if (juce::isPositiveAndBelow (c.source, numSources)) plugged[(size_t) c.source] = true;
    for (int d = 0; d < numDests; ++d)
    {
        const auto& n = normals[(size_t) d];
        if (n.source >= 0 && edit.into[(size_t) d].empty() && ! (n.full && plugged[(size_t) n.source]))
            m.into[(size_t) d].push_back ({ n.source, 1.0f });
    }
    return m;
}

std::vector<std::pair<int, Cord>> Patchbay::getLiveCords() const
{
    const auto m = live_();
    std::vector<std::pair<int, Cord>> all;
    for (int d = 0; d < numDests; ++d)
        for (auto& c : m.into[(size_t) d])
            all.push_back ({ d, c });
    return all;
}

bool Patchbay::isLive (int source, int dest) const
{
    if (! juce::isPositiveAndBelow (dest, numDests)) return false;
    if (isConnected (source, dest)) return true;
    return normals[(size_t) dest].source == source && isNormalLive (dest);
}

int Patchbay::normalOf (int dest) const
{
    return juce::isPositiveAndBelow (dest, numDests) ? normals[(size_t) dest].source : -1;
}

bool Patchbay::isFullNormal (int dest) const
{
    return juce::isPositiveAndBelow (dest, numDests) && normals[(size_t) dest].full;
}

bool Patchbay::isSourcePlugged (int source) const
{
    for (auto& cords : edit.into)
        for (auto& c : cords) if (c.source == source) return true;
    return false;
}

bool Patchbay::isDestPlugged (int dest) const
{
    return juce::isPositiveAndBelow (dest, numDests) && ! edit.into[(size_t) dest].empty();
}

bool Patchbay::isNormalLive (int dest) const
{
    const int s = normalOf (dest);
    if (s < 0 || isDestPlugged (dest)) return false;
    return ! (normals[(size_t) dest].full && isSourcePlugged (s));
}

void Patchbay::setNormal (int source, int dest, bool full)
{
    if (! juce::isPositiveAndBelow (dest, numDests)) return;
    if (source < 0) normals[(size_t) dest] = {};
    else if (juce::isPositiveAndBelow (source, numSources) && isMidiSource (source) == isMidiDest (dest))
        normals[(size_t) dest] = { source, full };
    else return;
    if (! wiringNormals) publish();
}

void Patchbay::clearNormals()
{
    for (auto& n : normals) n = {};
    if (! wiringNormals) publish();
}

std::vector<std::pair<int, int>> Patchbay::getAllNormals() const
{
    std::vector<std::pair<int, int>> all;
    for (int d = 0; d < numDests; ++d)
        if (normals[(size_t) d].source >= 0) all.push_back ({ d, normals[(size_t) d].source });
    return all;
}

std::unique_ptr<juce::XmlElement> Patchbay::toXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("PATCH");
    xml->setAttribute ("version", 6);
    auto where = [this] (int flat, bool isSource) -> std::pair<juce::String, juce::String>
    {
        for (auto& p : ports)
        {
            const int first = isSource ? p.firstSource : p.firstDest;
            const auto& names = isSource ? p.sources : p.dests;
            if (flat >= first && flat < first + names.size()) return { p.id, names[flat - first] };
        }
        return {};
    };

    for (auto& [d, c] : getAllCords())
    {
        auto [sp, sc] = where (c.source, true);
        auto [dp, dc] = where (d, false);
        auto* e = xml->createNewChildElement ("CORD");
        e->setAttribute ("fromPort", sp);
        e->setAttribute ("from", sc);
        e->setAttribute ("toPort", dp);
        e->setAttribute ("to", dc);
        if (c.gain != 1.0f) e->setAttribute ("gain", (double) c.gain);
    }
    for (int d = 0; d < numDests; ++d)
    {
        const auto& n = normals[(size_t) d];
        if (n.source < 0) continue;
        auto [sp, sc] = where (n.source, true);
        auto [dp, dc] = where (d, false);
        auto* e = xml->createNewChildElement ("NORMAL");
        e->setAttribute ("fromPort", sp);
        e->setAttribute ("from", sc);
        e->setAttribute ("toPort", dp);
        e->setAttribute ("to", dc);
        if (n.full) e->setAttribute ("full", true);
    }
    return xml;
}

void Patchbay::fromXml (const juce::XmlElement& xml)
{
    // (version 3: the click and LTC out were Hawthorne's and Route's; they're MELVILLE's clock's now)
    juce::XmlElement x (xml);
    if (x.getIntAttribute ("version") < 4)
        for (auto* e : x.getChildWithTagNameIterator ("CORD"))
        {
            if (e->getStringAttribute ("fromPort") == "hawthorne" && e->getStringAttribute ("from") == "Click")
                e->setAttribute ("fromPort", "clock");
            if (e->getStringAttribute ("fromPort") == "route" && e->getStringAttribute ("from") == "Hawthorne LTC")
                { e->setAttribute ("fromPort", "clock"); e->setAttribute ("from", "LTC"); }
        }
    fromXmlCurrent (x);
    // (before version 5 there was no MIDI on the patchbay: its usual cords, so nothing goes quiet)
    if (x.getIntAttribute ("version") < 5 && onOlderPatch) onOlderPatch();

    // (before version 6 there were no normals: the usual wiring was cables. Wire the usual
    //  normals; a cable that is just its input's normal becomes the normal, and a normal the
    //  old patch didn't have comes out, so everything is connected exactly as it was.)
    if (x.getIntAttribute ("version") < 6 && onDefaultNormals)
    {
        onDefaultNormals();
        for (int d = 0; d < numDests; ++d)
        {
            auto& n = normals[(size_t) d];
            if (n.source < 0) continue;
            auto& cords = edit.into[(size_t) d];
            if (cords.size() == 1 && cords.front().source == n.source && std::abs (cords.front().gain - 1.0f) < 1.0e-4f)
                cords.clear();
            else
                n = {};
        }
        publish();
    }
}

void Patchbay::fromXmlCurrent (const juce::XmlElement& xml)
{
    for (auto& c : edit.into) c.clear();
    for (auto& n : normals) n = {};
    for (auto* e : xml.getChildWithTagNameIterator ("NORMAL"))
    {
        const int s = findSource (e->getStringAttribute ("fromPort"), e->getStringAttribute ("from"));
        const int d = findDest (e->getStringAttribute ("toPort"), e->getStringAttribute ("to"));
        if (s >= 0 && d >= 0 && isMidiSource (s) == isMidiDest (d))
            normals[(size_t) d] = { s, e->getBoolAttribute ("full") };
    }
    for (auto* e : xml.getChildWithTagNameIterator ("CORD"))
    {
        const int s = findSource (e->getStringAttribute ("fromPort"), e->getStringAttribute ("from"));
        const int d = findDest (e->getStringAttribute ("toPort"), e->getStringAttribute ("to"));
        if (s >= 0 && d >= 0 && isMidiSource (s) == isMidiDest (d))
            edit.into[(size_t) d].push_back ({ s, (float) e->getDoubleAttribute ("gain", 1.0) });
    }
    publish();
}

void Patchbay::publish()
{
    auto* fresh = new Matrix (live_());
    auto* old = live.exchange (fresh, std::memory_order_acq_rel);
    if (old != nullptr)
        retired.push_back ({ old, blocksDone.load (std::memory_order_acquire) + 2 });
    if (onChange) onChange();
}

void Patchbay::collectGarbage (bool audioRunning)
{
    const auto done = blocksDone.load (std::memory_order_acquire);
    retired.erase (std::remove_if (retired.begin(), retired.end(), [&] (const Retired& r)
    {
        // two whole blocks after the swap the audio thread has let go of it
        if (! audioRunning || done >= r.afterBlock) { delete r.m; return true; }
        return false;
    }), retired.end());
}

} // namespace melville
