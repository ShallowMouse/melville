#pragma once

// MELVILLE ASIO: a virtual ASIO driver that other programs (REAPER, Guitar Rig, any DAW)
// pick as their audio device, the way ReaRoute works with REAPER. MELVILLE keeps the real
// interface; the program's 24 outputs arrive on MELVILLE's patchbay (the MELVILLE ASIO
// panel's OUT jacks), and its 24 inputs are whatever is patched to the panel's IN jacks.
//
// The two sides meet in shared memory. MELVILLE's audio callback is the clock: each block
// it writes what goes to the program, advances a frame counter and signals an event; the
// driver runs the program's ASIO callback from that, so both run sample-synchronously - no
// drift, no resampling. Without MELVILLE running the driver keeps time by itself (silence),
// so the program doesn't stall.
//
// Shared by MELVILLE (Source/Core/AsioNode.h) and the driver (Source/AsioDriver). Plain C++.

#include <atomic>
#include <cstdint>

namespace melville::asio
{
    constexpr std::uint32_t magic   = 0x4D415331;   // "MAS1"
    constexpr std::uint32_t version = 1;
    constexpr int channels   = 24;                  // each way
    constexpr int ringFrames = 1 << 15;             // 32768 frames per channel (power of two)
    constexpr int ringMask   = ringFrames - 1;

    constexpr const wchar_t* mappingName = L"Local\\MelvilleAsioV1";
    constexpr const wchar_t* tickName    = L"Local\\MelvilleAsioTickV1";

    constexpr const char* driverName = "MELVILLE ASIO";

    struct Shared
    {
        std::uint32_t magic, version;
        // MELVILLE's side
        std::atomic<std::int32_t> sampleRate;        // MELVILLE's device rate (0 = MELVILLE not running)
        std::atomic<std::int64_t> clock;             // frames MELVILLE's device has run (drives the driver)
        std::atomic<std::int64_t> heartbeatMs;       // MELVILLE's last callback (ms, monotonic): is it alive?
        std::atomic<std::int64_t> fromWrite;         // frames MELVILLE has written into fromMelville
        // the driver's side
        std::atomic<std::int32_t> driverRunning;     // a program has the driver started
        std::atomic<std::int32_t> driverBlock;       // its buffer size
        std::atomic<std::int64_t> toWrite;           // frames the driver has written into toMelville
        std::atomic<std::int64_t> driverClientId;    // changes when a program (re)starts the driver
        char clientName[64];                         // the program's name (its executable), for MELVILLE's patchbay

        float toMelville[channels][ringFrames];      // what the program plays (its outputs)
        float fromMelville[channels][ringFrames];    // what it gets (its inputs)
    };

    static_assert (std::atomic<std::int64_t>::is_always_lock_free, "shared-memory atomics must be lock-free");
}
