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
    publish();
}

void Patchbay::plug (int source, int dest, float gain)
{
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

void Patchbay::clearAll (Scope sc)
{
    for (size_t d = 0; d < edit.into.size(); ++d)
        if (inScope ((int) d, sc)) edit.into[d].clear();
    publish();
}

//==============================================================================
void Patchbay::setNormal (int source, int dest)
{
    if (! juce::isPositiveAndBelow (source, numSources) || ! juce::isPositiveAndBelow (dest, numDests)) return;
    if (isMidiSource (source) != isMidiDest (dest)) return;
    normals.erase (std::remove_if (normals.begin(), normals.end(), [dest] (auto& n) { return n.second == dest; }), normals.end());
    normals.push_back ({ source, dest });
    normalsChanged();
}

void Patchbay::removeNormal (int dest)
{
    const auto before = normals.size();
    normals.erase (std::remove_if (normals.begin(), normals.end(), [dest] (auto& n) { return n.second == dest; }), normals.end());
    if (normals.size() != before) normalsChanged();
}

void Patchbay::clearNormals (Scope sc)
{
    normals.erase (std::remove_if (normals.begin(), normals.end(), [this, sc] (auto& n) { return inScope (n.second, sc); }), normals.end());
    normalsChanged();
}

int Patchbay::normalInto (int dest) const
{
    for (auto& [s, d] : normals) if (d == dest) return s;
    return -1;
}

std::unique_ptr<juce::XmlElement> Patchbay::normalsToXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("BEHIND");
    xml->setAttribute ("version", 1);
    for (auto& [s, d] : normals)
    {
        auto [sp, sc] = where (s, true);
        auto [dp, dc] = where (d, false);
        auto* e = xml->createNewChildElement ("NORMAL");
        e->setAttribute ("fromPort", sp);
        e->setAttribute ("from", sc);
        e->setAttribute ("toPort", dp);
        e->setAttribute ("to", dc);
    }
    return xml;
}

void Patchbay::normalsFromXml (const juce::XmlElement& xml)
{
    normals.clear();
    for (auto* e : xml.getChildWithTagNameIterator ("NORMAL"))
    {
        const int s = findSource (e->getStringAttribute ("fromPort"), e->getStringAttribute ("from"));
        const int d = findDest (e->getStringAttribute ("toPort"), e->getStringAttribute ("to"));
        if (s >= 0 && d >= 0 && isMidiSource (s) == isMidiDest (d) && normalInto (d) < 0)
            normals.push_back ({ s, d });
    }
    publish();          // (loading them doesn't save them again)
}

std::vector<std::pair<int, Cord>> Patchbay::getLiveCords() const
{
    std::vector<std::pair<int, Cord>> all;
    if (auto* m = live.load (std::memory_order_acquire))
        for (int d = 0; d < (int) m->into.size(); ++d)
            for (auto& c : m->into[(size_t) d])
                all.push_back ({ d, c });
    return all;
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
    publish();
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
    publish();
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

std::unique_ptr<juce::XmlElement> Patchbay::wiringToXml() const
{
    auto xml = toXml();
    xml->setTagName ("WIRING");
    return xml;
}

void Patchbay::wiringFromXml (const juce::XmlElement& xml)
{
    loadCordsAndNormals (xml);      // (a NORMALS file from before: its normals as cables)
}

std::pair<juce::String, juce::String> Patchbay::where (int flat, bool isSource) const
{
    for (auto& p : ports)
    {
        const int first = isSource ? p.firstSource : p.firstDest;
        const auto& names = isSource ? p.sources : p.dests;
        if (flat >= first && flat < first + names.size()) return { p.id, names[flat - first] };
    }
    return {};
}

std::unique_ptr<juce::XmlElement> Patchbay::toXml (Scope sc) const
{
    auto xml = std::make_unique<juce::XmlElement> ("PATCH");
    xml->setAttribute ("version", 7);
    if (sc != Scope::all) xml->setAttribute ("scope", sc == Scope::midi ? "midi" : "audio");
    for (auto& [d, c] : getAllCords())
    {
        if (! inScope (d, sc)) continue;
        auto [sp, scName] = where (c.source, true);
        auto [dp, dc] = where (d, false);
        auto* e = xml->createNewChildElement ("CORD");
        e->setAttribute ("fromPort", sp);
        e->setAttribute ("from", scName);
        e->setAttribute ("toPort", dp);
        e->setAttribute ("to", dc);
        if (c.gain != 1.0f) e->setAttribute ("gain", (double) c.gain);
    }
    return xml;
}

void Patchbay::fromXml (const juce::XmlElement& xml, Scope sc)
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
    // (jacks since renamed: THOREAU is FULLER now, and POE has four sequencers)
    for (auto* e : x.getChildWithTagNameIterator ("CORD"))
        if (e->getStringAttribute ("fromPort") == "midi")
        {
            if (e->getStringAttribute ("from") == "THOREAU steps") e->setAttribute ("from", "FULLER steps");
            if (e->getStringAttribute ("from") == "POE")           e->setAttribute ("from", "POE 1");
        }
    fromXmlCurrent (x, sc);
    // (before version 5 there was no MIDI on the patchbay: its usual cords, so nothing goes quiet)
    if (x.getIntAttribute ("version") < 5 && sc != Scope::audio && onOlderPatch) onOlderPatch();

    // (version 6 had normals behind the bay: they came in above as cables, so it sounds as it did)
}

void Patchbay::fromXmlCurrent (const juce::XmlElement& xml, Scope sc)
{
    loadCordsAndNormals (xml, sc);
}

void Patchbay::loadCordsAndNormals (const juce::XmlElement& xml, Scope sc)
{
    for (size_t d = 0; d < edit.into.size(); ++d)
        if (inScope ((int) d, sc)) edit.into[d].clear();
    for (auto* e : xml.getChildWithTagNameIterator ("CORD"))
    {
        const int s = findSource (e->getStringAttribute ("fromPort"), e->getStringAttribute ("from"));
        const int d = findDest (e->getStringAttribute ("toPort"), e->getStringAttribute ("to"));
        if (s >= 0 && d >= 0 && isMidiSource (s) == isMidiDest (d) && inScope (d, sc))
            edit.into[(size_t) d].push_back ({ s, (float) e->getDoubleAttribute ("gain", 1.0) });
    }
    // normals (version 6): each one that was carrying becomes a cable
    std::vector<std::pair<int, int>> carrying;
    for (auto* e : xml.getChildWithTagNameIterator ("NORMAL"))
    {
        const int s = findSource (e->getStringAttribute ("fromPort"), e->getStringAttribute ("from"));
        const int d = findDest (e->getStringAttribute ("toPort"), e->getStringAttribute ("to"));
        if (s < 0 || d < 0 || isMidiSource (s) != isMidiDest (d) || ! inScope (d, sc) || ! edit.into[(size_t) d].empty()) continue;
        if (e->getBoolAttribute ("full") && isSourcePlugged (s)) continue;
        carrying.push_back ({ s, d });
    }
    for (auto [s, d] : carrying) edit.into[(size_t) d].push_back ({ s, 1.0f });
    publish();
}

void Patchbay::publish()
{
    auto* fresh = new Matrix (edit);
    // the normals behind the bay carry into every input with no cable in it
    for (auto& [s, d] : normals)
        if (juce::isPositiveAndBelow (d, (int) fresh->into.size()) && edit.into[(size_t) d].empty())
            fresh->into[(size_t) d].push_back ({ s, 1.0f });
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
