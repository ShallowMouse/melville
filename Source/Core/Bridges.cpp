#include "Bridges.h"
#include <cstring>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#endif

namespace melville
{

namespace route = hawthorne::route;
namespace link = emerson::link;

//==============================================================================
void* SharedMemory::open (const wchar_t* name, size_t bytes)
{
    close();
   #if JUCE_WINDOWS
    if (name != nullptr)
    {
        auto* m = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      (DWORD) ((unsigned long long) bytes >> 32), (DWORD) (bytes & 0xffffffffu), name);
        if (m == nullptr) return nullptr;
        view = MapViewOfFile (m, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        if (view == nullptr) { CloseHandle (m); return nullptr; }
        mapping = m;
        return view;
    }
   #else
    juce::ignoreUnused (name);
   #endif
    heap.reset (new char[bytes]);
    std::memset (heap.get(), 0, bytes);
    return heap.get();
}

void SharedMemory::close()
{
   #if JUCE_WINDOWS
    if (view != nullptr)    { UnmapViewOfFile (view); view = nullptr; }
    if (mapping != nullptr) { CloseHandle ((HANDLE) mapping); mapping = nullptr; }
   #endif
    heap.reset();
}

//==============================================================================
namespace names
{
    juce::StringArray numbered (const juce::String& prefix, int n, int first)
    {
        juce::StringArray s;
        for (int i = 0; i < n; ++i) s.add (prefix + juce::String (first + i));
        return s;
    }

    juce::StringArray routeFromHawthorne()
    {
        auto s = numbered ("Hawthorne track ", route::maxTracks);
        s.addArray ({ "Hawthorne master L", "Hawthorne master R", "Hawthorne LTC" });
        return s;
    }

    juce::StringArray routeToHawthorne()
    {
        auto s = numbered ("To Hawthorne track ", route::maxTracks);
        s.addArray ({ "To Hawthorne master L", "To Hawthorne master R" });
        return s;
    }

    juce::StringArray linkToWhitman()
    {
        auto s = numbered ("Hawthorne track ", link::maxTracks);
        s.addArray ({ "EMERSON return A L", "EMERSON return A R", "EMERSON return B L", "EMERSON return B R" });
        return s;
    }

    juce::StringArray linkFromWhitman()
    {
        return { "To Hawthorne master L", "To Hawthorne master R",
                 "To EMERSON send A L", "To EMERSON send A R", "To EMERSON send B L", "To EMERSON send B R" };
    }
}

//==============================================================================
RouteBridge::RouteBridge()
{
    port.id = "route";
    port.title = "HAWTHORNE ROUTE";
    port.sources = names::routeFromHawthorne();   // in the same order as fromHawthorne's slots
    port.dests = names::routeToHawthorne();       // ... and toHawthorne's
    static_assert (route::fromHawthorneChannels == route::maxTracks + 3);
    static_assert (route::toHawthorneChannels == route::maxTracks + 2);
}

bool RouteBridge::open()
{
    shared = static_cast<route::Shared*> (memory.open (route::mappingName, sizeof (route::Shared)));
    if (shared == nullptr) return false;
    stamp();
    return true;
}

void RouteBridge::stamp() noexcept
{
    if (shared == nullptr) return;
    if (shared->magic != route::magic) { shared->magic = route::magic; shared->version = route::version; }
    std::strncpy (shared->clientName, "MELVILLE", sizeof (shared->clientName) - 1);
}

int RouteBridge::getNumTracks() const noexcept
{
    return shared != nullptr && shared->sampleRate.load() > 0 ? route::tracksOf (shared->numTracks.load()) : 0;
}

void RouteBridge::prepare (double, int maxBlock)
{
    if (shared == nullptr) return;
    shared->driverBlock = maxBlock;
    shared->driverClientId.fetch_add (1);     // Hawthorne relocks its ROUTE inputs
    shared->driverRunning = 1;
    lastFrom = -1;
}

void RouteBridge::release()
{
    if (shared != nullptr) shared->driverRunning = 0;
}

void RouteBridge::pull (float* const* sources, int n) noexcept
{
    const int numCh = port.sources.size();
    const auto from = shared != nullptr ? shared->fromWrite.load (std::memory_order_acquire) : -1;
    // Hawthorne writes exactly one block each callback; if it didn't run this block, silence
    if (shared == nullptr || from < n || from == lastFrom)
    {
        for (int c = 0; c < numCh; ++c) juce::FloatVectorOperations::clear (sources[c], n);
        lastFrom = from;
        return;
    }
    lastFrom = from;
    const auto start = from - n;
    const int p = (int) (start & route::ringMask);
    const int first = juce::jmin (n, route::ringFrames - p);
    for (int c = 0; c < numCh; ++c)
    {
        std::memcpy (sources[c], shared->fromHawthorne[c] + p, sizeof (float) * (size_t) first);
        if (first < n) std::memcpy (sources[c] + first, shared->fromHawthorne[c], sizeof (float) * (size_t) (n - first));
    }
}

void RouteBridge::push (const float* const* dests, int n) noexcept
{
    if (shared == nullptr) return;
    const auto w = shared->toWrite.load (std::memory_order_relaxed);
    const int p = (int) (w & route::ringMask);
    const int first = juce::jmin (n, route::ringFrames - p);
    for (int c = 0; c < port.dests.size(); ++c)
    {
        std::memcpy (shared->toHawthorne[c] + p, dests[c], sizeof (float) * (size_t) first);
        if (first < n) std::memcpy (shared->toHawthorne[c], dests[c] + first, sizeof (float) * (size_t) (n - first));
    }
    shared->driverBlock.store (n, std::memory_order_relaxed);
    shared->toWrite.store (w + n, std::memory_order_release);
}

//==============================================================================
LinkBridge::LinkBridge()
{
    port.id = "link";
    port.title = "EMERSON LINK";
    port.sources = names::linkToWhitman();       // toWhitman's slots, in order
    port.dests = names::linkFromWhitman();       // fromWhitman's slots, in order
    static_assert (link::toWhitmanChannels == link::maxTracks + 4);
    static_assert (link::fromWhitmanChannels == 6);
}

bool LinkBridge::open()
{
    shared = static_cast<link::Shared*> (memory.open (link::mappingName, sizeof (link::Shared)));
    if (shared == nullptr) return false;
    stamp();
    return true;
}

void LinkBridge::stamp() noexcept
{
    if (shared == nullptr) return;
    if (shared->magic != link::magic) { shared->magic = link::magic; shared->version = link::version; }
    std::strncpy (shared->clientName, "MELVILLE", sizeof (shared->clientName) - 1);
}

void LinkBridge::prepare (double, int)
{
    if (shared == nullptr) return;
    shared->clientId.fetch_add (1);
    shared->clientRunning = 1;
    readPos = -1;
    lastWrite = -1;
}

void LinkBridge::release()
{
    if (shared != nullptr) shared->clientRunning = 0;
}

void LinkBridge::pull (float* const* sources, int n) noexcept
{
    const int numCh = port.sources.size();
    readPos = -1;
    if (shared != nullptr)
    {
        // EMERSON writes one block per callback, [w - n, w); WHITMAN answers in the same frames
        const auto w = shared->toWrite.load (std::memory_order_acquire);
        if (w != lastWrite && w >= n) readPos = w - n;
        lastWrite = w;
    }
    if (readPos < 0)
    {
        for (int c = 0; c < numCh; ++c) juce::FloatVectorOperations::clear (sources[c], n);
        return;
    }
    const int p = (int) (readPos & link::ringMask);
    const int first = juce::jmin (n, link::ringFrames - p);
    for (int c = 0; c < numCh; ++c)
    {
        std::memcpy (sources[c], shared->toWhitman[c] + p, sizeof (float) * (size_t) first);
        if (first < n) std::memcpy (sources[c] + first, shared->toWhitman[c], sizeof (float) * (size_t) (n - first));
    }
}

void LinkBridge::push (const float* const* dests, int n) noexcept
{
    if (shared == nullptr || readPos < 0) return;
    const int p = (int) (readPos & link::ringMask);
    const int first = juce::jmin (n, link::ringFrames - p);
    for (int c = 0; c < port.dests.size(); ++c)
    {
        std::memcpy (shared->fromWhitman[c] + p, dests[c], sizeof (float) * (size_t) first);
        if (first < n) std::memcpy (shared->fromWhitman[c], dests[c] + first, sizeof (float) * (size_t) (n - first));
    }
    shared->fromWrite.store (readPos + n, std::memory_order_release);
}

//==============================================================================
InterfaceNode::InterfaceNode (int numIns, int numOuts)
{
    port.id = "interface";
    port.title = "INTERFACE";
    port.sources = names::numbered ("In ", numIns);
    port.dests = names::numbered ("Out ", numOuts);
}

void InterfaceNode::pull (float* const* sources, int n) noexcept
{
    for (int c = 0; c < port.sources.size(); ++c)
    {
        if (c < numIn && in != nullptr && in[c] != nullptr) juce::FloatVectorOperations::copy (sources[c], in[c], n);
        else juce::FloatVectorOperations::clear (sources[c], n);
    }
}

void InterfaceNode::push (const float* const* dests, int n) noexcept
{
    for (int c = 0; c < numOut; ++c)
        if (out != nullptr && out[c] != nullptr)
        {
            if (c < port.dests.size()) juce::FloatVectorOperations::copy (out[c], dests[c], n);
            else juce::FloatVectorOperations::clear (out[c], n);
        }
}

} // namespace melville
