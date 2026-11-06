#include "MelvilleHosted.h"
#include "../Core/Hub.h"
#include "../Core/PartDevice.h"

namespace melville
{

bool isHosted() noexcept { return Hub::getIfExists() != nullptr; }

bool attach (juce::AudioDeviceManager& dm, const char* partId)
{
    auto* hub = Hub::getIfExists();
    if (hub == nullptr) return false;
    auto* part = hub->findPart (partId);
    if (part == nullptr) { jassertfalse; return false; }

    part->manager = &dm;
    // only MELVILLE's type: the manager never scans for (or grabs) the interface itself
    dm.addAudioDeviceType (std::make_unique<PartDeviceType> (*hub, part->port.id));
    dm.initialise ((int) part->visibleIns.size(), (int) part->visibleOuts.size(), nullptr, false, part->deviceName);
    dm.setCurrentAudioDeviceType (PartDeviceType::typeName, true);

    auto setup = dm.getAudioDeviceSetup();
    setup.inputDeviceName = setup.outputDeviceName = part->deviceName;
    setup.sampleRate = hub->getSampleRate();
    setup.bufferSize = hub->getBlockSize();
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange (0, (int) part->visibleIns.size(), true);
    setup.outputChannels.setRange (0, (int) part->visibleOuts.size(), true);
    const auto error = dm.setAudioDeviceSetup (setup, true);
    jassert (error.isEmpty());
    return true;
}

void detach (juce::AudioDeviceManager& dm)
{
    if (auto* hub = Hub::getIfExists())
        for (auto& n : hub->getNodes())
            if (auto* p = dynamic_cast<PartNode*> (n.get()))
                if (p->manager == &dm) p->manager = nullptr;
}

} // namespace melville
