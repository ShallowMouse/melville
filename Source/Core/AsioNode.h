#pragma once

// MELVILLE's end of the MELVILLE ASIO driver (Source/AsioDriver): a program that picks
// "MELVILLE ASIO" as its audio device (REAPER, Guitar Rig...) gets a panel on the patchbay.
//   PULL (at the top of the block): what the program played -> sources "Out 1".."Out 24"
//   PUSH (at the end):              destinations "In 1".."In 24" -> the program's inputs,
//                                   then the clock advances and the driver is woken
// MELVILLE's callback is the clock, so the program runs sample-locked to the interface. Its
// outputs come back one of its buffers (plus one of ours) later.

#include "Bridges.h"
#include "../Asio/MelvilleAsioShared.h"

namespace melville
{

class AsioNode : public Node
{
public:
    AsioNode();
    ~AsioNode() override;

    void prepare (double sampleRate, int maxBlock) override;
    void release() override;
    void pull (float* const* sources, int n) noexcept override;
    void push (const float* const* dests, int n) noexcept override;

    /** Is a program running the driver now? (Any thread.) */
    bool isConnected() const noexcept;
    /** Its name ("reaper", "Guitar Rig 7"...), when connected. (Message thread.) */
    juce::String clientName() const;
    /** How far behind MELVILLE its outputs arrive, in samples. */
    int getLatency() const noexcept { return latency.load(); }

    /** The driver: is it registered (so other programs can pick it)? Is it this version's? */
    static bool isDriverInstalled();
    static bool isDriverCurrent();
    /** Put the bundled driver in place and register it (Windows asks for permission). Returns an error, or empty. */
    static juce::String installDriver();
    static juce::String uninstallDriver();

    asio::Shared* getShared() const noexcept { return shared; }     // (tests)

private:
    void open();
    SharedMemory memory;
    asio::Shared* shared = nullptr;
    void* tick = nullptr;                 // (a Windows event)
    juce::int64 readPos = -1, clientId = -1;
    std::atomic<int> latency { 0 };
};

} // namespace melville
