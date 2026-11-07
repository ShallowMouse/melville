#pragma once

// The "MELVILLE" audio device each part opens instead of the interface. It has the
// part's own channel names (EMERSON's are Hawthorne Route's, WHITMAN's are the EMERSON
// Link's), so each app's routing code works as it does on its own. Its sample rate and
// buffer size are the interface's, and its callback runs inside the hub's.

#include <juce_audio_devices/juce_audio_devices.h>
#include "Node.h"

namespace melville
{

class Hub;

class PartDevice : public juce::AudioIODevice
{
public:
    PartDevice (Hub&, PartNode&);
    ~PartDevice() override;

    PartNode& getPart() noexcept { return part; }

    juce::StringArray getOutputChannelNames() override { return names (part.port.sources, part.visibleOuts); }
    juce::StringArray getInputChannelNames() override  { return names (part.port.dests, part.visibleIns); }
    std::optional<juce::BigInteger> getDefaultOutputChannels() const override { return all ((int) part.visibleOuts.size()); }
    std::optional<juce::BigInteger> getDefaultInputChannels() const override  { return all ((int) part.visibleIns.size()); }
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;
    int getDefaultBufferSize() override;

    juce::String open (const juce::BigInteger& inputChannels, const juce::BigInteger& outputChannels,
                       double sampleRate, int bufferSizeSamples) override;
    void close() override;
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback*) override;
    void stop() override;
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return lastError; }
    int getCurrentBufferSizeSamples() override;
    double getCurrentSampleRate() override;
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return activeOut; }
    juce::BigInteger getActiveInputChannels() const override  { return activeIn; }
    // the interface's own latency: the parts run in its callback, so theirs is the same
    // (Hawthorne lines overdubs up with it)
    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;

    //--------------------------------------------------------------------------
    // From the hub
    /** The interface (re)started or changed rate / block: tell the part. Message thread. */
    void interfaceRestarted();
    void interfaceStopped();
    /** Audio thread: run the part's callback for one block. */
    void run (const float* const* dests, float* const* sources, int n) noexcept;

private:
    static juce::BigInteger all (int n) { juce::BigInteger b; b.setRange (0, n, true); return b; }
    static juce::StringArray names (const juce::StringArray& from, const std::vector<int>& visible)
    {
        juce::StringArray s;
        for (int i : visible) s.add (from[i]);
        return s;
    }

    Hub& hub;
    PartNode& part;
    bool opened = false;
    juce::String lastError;
    juce::BigInteger activeIn, activeOut;
    std::vector<int> inMap, outMap;            // active channel k -> port channel number
    std::vector<bool> outUsed;                 // port channels the part writes
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;
    juce::AudioIODeviceCallback* callback = nullptr;
    juce::SpinLock callbackLock;
};

//==============================================================================
/** The "MELVILLE" device type: one device per part. Hosted apps get only this type. */
class PartDeviceType : public juce::AudioIODeviceType
{
public:
    explicit PartDeviceType (Hub&, juce::String onlyPart = {});

    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool wantInputNames = false) const override;
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice*, bool asInput) const override;
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& outputDeviceName, const juce::String& inputDeviceName) override;

    static constexpr const char* typeName = "MELVILLE";

private:
    Hub& hub;
    juce::String onlyPart;   // a hosted app sees just its own part
};

} // namespace melville
