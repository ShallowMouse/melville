#pragma once

// MELVILLE's end of the two links the apps already speak, so their engines stay as they are.
//
// RouteBridge plays the Hawthorne Route driver's part. Hawthorne's RouteHost writes its
// tracks into the Route memory and reads its ROUTE inputs from it; inside MELVILLE the
// bridge, not the ASIO driver, is on the other end:
//   PULL (right after Hawthorne): the block Hawthorne just wrote -> sources "Hawthorne track N" ...
//   PUSH (after EMERSON):         destinations "To Hawthorne track N" ... -> Hawthorne's ROUTE inputs
//
// LinkBridge plays WHITMAN's "EMERSON Link" device. EMERSON's LinkHost writes its
// processed tracks and FX returns into the link memory each block:
//   PULL (right after EMERSON): that block -> sources "Hawthorne track N", "EMERSON return A L" ...
//   PUSH (after WHITMAN):       destinations "To Hawthorne master L", "To EMERSON send A L" ... -> EMERSON
//
// In MELVILLE the memory is MELVILLE's own: everything in MELVILLE is built with
// MELVILLE's mapping names (HAWTHORNE_ROUTE_MAPPING etc., set in CMakeLists.txt), so a
// standalone Hawthorne or EMERSON running at the same time doesn't collide with it.

#include "Node.h"
#include "RouteShared.h"      // Hawthorne: Source/Route
#include "EmersonLink.h"      // EMERSON: Source/Link

namespace melville
{

class SharedMemory
{
public:
    ~SharedMemory() { close(); }
    /** A named mapping (Windows), or plain zeroed memory elsewhere / in tests. */
    void* open (const wchar_t* name, size_t bytes);
    void close();
private:
    void* mapping = nullptr;
    void* view = nullptr;
    std::unique_ptr<char[]> heap;
};

//==============================================================================
class RouteBridge : public Node
{
public:
    RouteBridge();

    /** Open MELVILLE's Route memory (Hawthorne's RouteHost opens the same). */
    bool open();
    /** Tests: use this memory. */
    void attach (hawthorne::route::Shared* s) noexcept { shared = s; stamp(); }

    /** The reel's track count Hawthorne publishes (0 = Hawthorne isn't running). */
    int getNumTracks() const noexcept;
    bool isHawthorneRunning() const noexcept { return shared != nullptr && shared->sampleRate.load() > 0; }

    void prepare (double sampleRate, int maxBlock) override;
    void release() override;
    void pull (float* const* sources, int n) noexcept override;
    void push (const float* const* dests, int n) noexcept override;

    static constexpr int numTracks = hawthorne::route::maxTracks;
    hawthorne::route::Shared* getShared() const noexcept { return shared; }

private:
    void stamp() noexcept;
    SharedMemory memory;
    hawthorne::route::Shared* shared = nullptr;
    std::int64_t lastFrom = -1;
};

//==============================================================================
class LinkBridge : public Node
{
public:
    LinkBridge();

    bool open();
    void attach (emerson::link::Shared* s) noexcept { shared = s; stamp(); }
    bool isEmersonRunning() const noexcept { return shared != nullptr && shared->sampleRate.load() > 0; }
    emerson::link::Shared* getShared() const noexcept { return shared; }

    void prepare (double sampleRate, int maxBlock) override;
    void release() override;
    void pull (float* const* sources, int n) noexcept override;
    void push (const float* const* dests, int n) noexcept override;

private:
    void stamp() noexcept;
    SharedMemory memory;
    emerson::link::Shared* shared = nullptr;
    std::int64_t readPos = -1;     // start of the block pulled this time (-1: nothing)
    std::int64_t lastWrite = -1;   // EMERSON's toWrite last block
};

//==============================================================================
/** The audio interface's channels: its inputs are sources, its outputs destinations. */
class InterfaceNode : public Node
{
public:
    InterfaceNode (int numIns, int numOuts);
    void setNames (const juce::StringArray& ins, const juce::StringArray& outs);
    const float* const* in = nullptr;  int numIn = 0;     // this block (set by the hub)
    float* const* out = nullptr;       int numOut = 0;
    void pull (float* const* sources, int n) noexcept override;
    void push (const float* const* dests, int n) noexcept override;
};

/** The channel names both ends of each link use (the apps' devices show the same). */
namespace names
{
    juce::StringArray routeFromHawthorne();   // "Hawthorne track 1".."96", "Hawthorne master L/R", "Hawthorne LTC"
    juce::StringArray routeToHawthorne();     // "To Hawthorne track 1".."96", "To Hawthorne master L/R"
    juce::StringArray linkToWhitman();        // "Hawthorne track 1".."96", "EMERSON return A L/R", "EMERSON return B L/R"
    juce::StringArray linkFromWhitman();      // "To Hawthorne master L/R", "To EMERSON send A L/R", "To EMERSON send B L/R"
    juce::StringArray numbered (const juce::String& prefix, int n, int first = 1);
}

} // namespace melville
