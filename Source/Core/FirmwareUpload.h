#pragma once

// Putting new firmware on a hardware control surface (the HAWTHORNE transport): the signed file
// MELVILLE carries is posted to http://<box>/update with the key the box gave when it was paired.
// The box writes it to its spare half of the flash as it arrives and switches to it only if all of
// it went in and its signature checks out, so an update that fails or breaks off leaves the old
// firmware running. Runs on its own thread; the INTERFACE window shows how far it's got.

#include <juce_core/juce_core.h>

namespace melville
{

class FirmwareUpload : private juce::Thread
{
public:
    enum State { sending, done, failed };

    FirmwareUpload (juce::String boxHost, juce::String boxKey, juce::File firmware, juce::String firmwareVersion)
        : juce::Thread ("MELVILLE firmware update"), host (std::move (boxHost)), version (std::move (firmwareVersion)),
          key (std::move (boxKey)), file (std::move (firmware))
    {
        startThread();
    }

    ~FirmwareUpload() override { stopThread (20000); }   // (stopping mid-way: the box keeps its old firmware)

    const juce::String host, version;

    State getState() const noexcept   { return state.load(); }
    float getProgress() const noexcept { return sent.load(); }      // 0..1, how much has gone
    juce::String getMessage() const    { const juce::ScopedLock sl (lock); return message; }

    /** One line for the device list. */
    juce::String describe() const
    {
        switch (getState())
        {
            case sending: return sent.load() >= 0.999f ? "installing " + version + "..."
                                                       : "updating to " + version + ": " + juce::String (juce::roundToInt (sent.load() * 100.0f)) + " %";
            case done:    return "updated to " + version + ": restarting";
            default:      return "update failed: " + getMessage();
        }
    }

private:
    void run() override
    {
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data) || data.getSize() < 4096)
            return fail ("couldn't read " + file.getFileName());

        int status = 0;
        const auto url = juce::URL ("http://" + host + "/update").withPOSTData (data);
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                 .withExtraHeaders ("Content-Type: application/octet-stream\r\nX-Hawthorne-Key: " + key)
                                 .withConnectionTimeoutMs (60000)   // (the box answers once it has written and checked it all)
                                 .withStatusCode (&status)
                                 .withProgressCallback ([this] (int done, int total)
                                 {
                                     if (total > 0) sent = (float) done / (float) total;
                                     return ! threadShouldExit();
                                 });
        auto in = url.createInputStream (options);
        if (threadShouldExit()) return fail ("stopped (the box keeps its old firmware)");
        if (in == nullptr) return fail ("couldn't reach the box at " + host);
        const auto reply = in->readEntireStreamAsString().trim();
        if (status == 200) { sent = 1.0f; state = done; return; }
        if (status == 403) return fail (reply.isNotEmpty() ? reply : "the box has a different key: pair it again");
        fail (reply.isNotEmpty() ? reply : "the box answered " + juce::String (status));
    }

    void fail (const juce::String& why)
    {
        {
            const juce::ScopedLock sl (lock);
            message = why;
        }
        state = failed;
    }

    const juce::String key;
    const juce::File file;
    std::atomic<State> state { sending };
    std::atomic<float> sent { 0.0f };
    juce::CriticalSection lock;
    juce::String message;
};

} // namespace melville
