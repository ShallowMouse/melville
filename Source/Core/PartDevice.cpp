#include "PartDevice.h"
#include "Hub.h"

namespace melville
{

//==============================================================================
void PartNode::process (const float* const* dests, float* const* sources, int n) noexcept
{
    if (auto* d = device.load (std::memory_order_acquire))
        d->run (dests, sources, n);
    else
        for (int c = 0; c < port.sources.size(); ++c)
            juce::FloatVectorOperations::clear (sources[c], n);
}

//==============================================================================
PartDevice::PartDevice (Hub& h, PartNode& p)
    : juce::AudioIODevice (p.deviceName, PartDeviceType::typeName), hub (h), part (p)
{
}

PartDevice::~PartDevice()
{
    close();
}

juce::Array<double> PartDevice::getAvailableSampleRates() { return { hub.getSampleRate() }; }
juce::Array<int> PartDevice::getAvailableBufferSizes()    { return { hub.getBlockSize() }; }
int PartDevice::getDefaultBufferSize()                    { return hub.getBlockSize(); }
int PartDevice::getCurrentBufferSizeSamples()             { return hub.getBlockSize(); }
double PartDevice::getCurrentSampleRate()                 { return hub.getSampleRate(); }

juce::String PartDevice::open (const juce::BigInteger& inputChannels, const juce::BigInteger& outputChannels, double, int)
{
    close();
    // the rate and block are the interface's, whatever the part asked for
    // (device channel numbers are positions in the visible lists)
    const int numIn = (int) part.visibleIns.size(), numOut = (int) part.visibleOuts.size();
    activeIn = inputChannels;
    activeOut = outputChannels;
    if (activeIn.getHighestBit() >= numIn)   activeIn.setRange (numIn, activeIn.getHighestBit() + 1 - numIn, false);
    if (activeOut.getHighestBit() >= numOut) activeOut.setRange (numOut, activeOut.getHighestBit() + 1 - numOut, false);

    inMap.clear();
    outMap.clear();
    outUsed.assign ((size_t) part.port.sources.size(), false);
    for (int c = 0; c < numIn; ++c)  if (activeIn[c])  inMap.push_back (part.visibleIns[(size_t) c]);
    for (int c = 0; c < numOut; ++c) if (activeOut[c]) { outMap.push_back (part.visibleOuts[(size_t) c]); outUsed[(size_t) outMap.back()] = true; }
    inPtrs.assign (inMap.size(), nullptr);
    outPtrs.assign (outMap.size(), nullptr);

    opened = true;
    hub.partDeviceOpened (*this);
    return {};
}

void PartDevice::close()
{
    if (! opened) return;
    stop();
    opened = false;
    hub.partDeviceClosed (*this);
}

void PartDevice::start (juce::AudioIODeviceCallback* cb)
{
    if (! opened || cb == nullptr) return;
    if (cb == callback) return;
    stop();
    cb->audioDeviceAboutToStart (this);
    const juce::SpinLock::ScopedLockType sl (callbackLock);
    callback = cb;
}

void PartDevice::stop()
{
    juce::AudioIODeviceCallback* old = nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (callbackLock);
        old = callback;
        callback = nullptr;
    }
    if (old != nullptr) old->audioDeviceStopped();
}

void PartDevice::interfaceRestarted()
{
    juce::AudioIODeviceCallback* cb = nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (callbackLock);
        cb = callback;
        callback = nullptr;
    }
    if (cb == nullptr) return;
    cb->audioDeviceStopped();
    cb->audioDeviceAboutToStart (this);   // picks up the new rate / block
    const juce::SpinLock::ScopedLockType sl (callbackLock);
    callback = cb;
}

void PartDevice::interfaceStopped()
{
    // the part keeps its callback; it just isn't called until the interface runs again
}

void PartDevice::run (const float* const* dests, float* const* sources, int n) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (callbackLock);
    const int numSources = part.port.sources.size();
    if (! sl.isLocked() || callback == nullptr)
    {
        for (int c = 0; c < numSources; ++c) juce::FloatVectorOperations::clear (sources[c], n);
        return;
    }

    for (size_t k = 0; k < inMap.size(); ++k)  inPtrs[k] = dests[inMap[k]];
    for (size_t k = 0; k < outMap.size(); ++k) outPtrs[k] = sources[outMap[k]];
    // channels the part didn't open stay silent
    for (int c = 0; c < numSources; ++c)
        if (! outUsed[(size_t) c]) juce::FloatVectorOperations::clear (sources[c], n);

    callback->audioDeviceIOCallbackWithContext (inPtrs.data(), (int) inPtrs.size(),
                                                outPtrs.data(), (int) outPtrs.size(), n, {});
}

//==============================================================================
PartDeviceType::PartDeviceType (Hub& h, juce::String only)
    : juce::AudioIODeviceType (typeName), hub (h), onlyPart (std::move (only))
{
}

juce::StringArray PartDeviceType::getDeviceNames (bool) const
{
    juce::StringArray names;
    for (auto& node : hub.getNodes())
        if (auto* p = dynamic_cast<PartNode*> (node.get()))
            if (onlyPart.isEmpty() || p->port.id == onlyPart)
                names.add (p->deviceName);
    return names;
}

int PartDeviceType::getIndexOfDevice (juce::AudioIODevice* d, bool) const
{
    return d != nullptr ? getDeviceNames().indexOf (d->getName()) : -1;
}

juce::AudioIODevice* PartDeviceType::createDevice (const juce::String& outputDeviceName, const juce::String& inputDeviceName)
{
    const auto name = outputDeviceName.isNotEmpty() ? outputDeviceName : inputDeviceName;
    for (auto& node : hub.getNodes())
        if (auto* p = dynamic_cast<PartNode*> (node.get()))
            if ((onlyPart.isEmpty() || p->port.id == onlyPart) && (name.isEmpty() || p->deviceName == name))
            {
                return new PartDevice (hub, *p);
            }
    return nullptr;
}

} // namespace melville
