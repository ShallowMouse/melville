#include "Hub.h"
#include "PartDevice.h"
#include "Bridges.h"
#include "NetworkNode.h"

namespace melville
{

namespace
{
    Hub* theHub = nullptr;
    juce::SpinLock structureLock;    // held by the audio thread for the whole block

    constexpr int interfaceChannels = 64;
    constexpr int hawthorneChannels = 64;
    constexpr int busChannels = 16;
}

Hub& Hub::get()
{
    jassert (theHub != nullptr);   // MELVILLE's Main.cpp makes the hub before any part
    return *theHub;
}

Hub* Hub::getIfExists() noexcept { return theHub; }

//==============================================================================
Hub::Hub()
{
    jassert (theHub == nullptr);
    theHub = this;

    auto iface = std::make_unique<InterfaceNode> (interfaceChannels, interfaceChannels);
    interfaceNode = iface.get();
    addNode (std::move (iface));

    // the parts, in the order they're shown
    auto hawthorneIns = names::numbered ("In ", hawthorneChannels);
    hawthorneIns.addArray (names::numbered ("MELVILLE bus ", busChannels));
    addNode (std::make_unique<PartNode> ("hawthorne", "HAWTHORNE", hawthorneIns, names::numbered ("Out ", hawthorneChannels)));
    addNode (std::make_unique<PartNode> ("emerson", "EMERSON", names::routeFromHawthorne(), names::routeToHawthorne()));
    addNode (std::make_unique<PartNode> ("whitman", "WHITMAN", names::linkToWhitman(), names::linkFromWhitman()));
    addNode (std::make_unique<PartNode> ("thoreau", "THOREAU", juce::StringArray(), names::numbered ("Out ", 16)));
    addNode (std::make_unique<PartNode> ("pequod", "PEQUOD HOST", names::numbered ("In ", 8), names::numbered ("Out ", 8)));

    // Hawthorne Link: audio from other PCs
    auto net = std::make_unique<NetworkNode>();
    network = net.get();
    addNode (std::move (net));

    // the links the apps already speak, played from MELVILLE's end
    auto route = std::make_unique<RouteBridge>();
    route->open();
    addNode (std::move (route));
    auto link = std::make_unique<LinkBridge>();
    link->open();
    addNode (std::move (link));

    auto node = [this] (const char* id) -> Node*
    {
        for (auto& n : nodes) if (n->port.id == id) return n.get();
        return nullptr;
    };

    // the running order (see Hub.h)
    steps = {
        { Step::pull,    node ("interface") },
        { Step::pull,    node ("network") },
        { Step::process, node ("pequod") },
        { Step::process, node ("thoreau") },
        { Step::process, node ("hawthorne") },
        { Step::pull,    node ("route") },      // Hawthorne's tracks, this block
        { Step::process, node ("emerson") },
        { Step::pull,    node ("link") },       // EMERSON's tracks and returns, this block
        { Step::process, node ("whitman") },
        { Step::push,    node ("link") },       // the mix and sends back to EMERSON
        { Step::push,    node ("route") },      // EMERSON's tracks and mix back into Hawthorne
        { Step::push,    node ("interface") },
    };

    findPart ("emerson")->deviceName = "Hawthorne Route (MELVILLE)";
    findPart ("whitman")->deviceName = "Hawthorne Route via EMERSON (MELVILLE)";

    updateVisibleTracks (hawthorne::route::defaultTracks);
    defaultPatch();
    prepare (sampleRate, blockSize, 0, 0);
    startTimerHz (10);
}

Hub::~Hub()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    for (auto& n : nodes) n->release();
    if (theHub == this) theHub = nullptr;
}

void Hub::addNode (std::unique_ptr<Node> n)
{
    n->portIndex = patchbay.addPort (n->port);
    nodes.push_back (std::move (n));
}

PartNode* Hub::findPart (const juce::String& id) const
{
    for (auto& n : nodes)
        if (auto* p = dynamic_cast<PartNode*> (n.get()))
            if (p->port.id == id) return p;
    return nullptr;
}

bool Hub::isPartConnected (const juce::String& id) const
{
    auto* p = findPart (id);
    return p != nullptr && p->device.load() != nullptr && p->device.load()->isPlaying();
}

//==============================================================================
void Hub::defaultPatch()
{
    auto& pb = patchbay;
    pb.clearAll();
    // the interface into Hawthorne and back out, channel for channel
    pb.connectInOrder ("interface", 0, "hawthorne", 0, hawthorneChannels);
    pb.connectInOrder ("hawthorne", 0, "interface", 0, hawthorneChannels);
    // Hawthorne -> EMERSON -> WHITMAN -> EMERSON -> Hawthorne, as Route and the Link do now
    pb.connectByName ("route", "emerson");
    // (EMERSON's processed tracks leave by the Link; only its mix chain goes back to Hawthorne here)
    for (auto side : { "L", "R" })
        pb.connect (pb.findSource ("emerson", juce::String ("To Hawthorne master ") + side),
                    pb.findDest ("route", juce::String ("To Hawthorne master ") + side));
    pb.connectByName ("link", "whitman");
    pb.connectByName ("whitman", "link");
    // THOREAU's stereo out onto MELVILLE buses 1-2, PEQUOD Host's onto 3-4 (Hawthorne records them from there)
    pb.connectInOrder ("thoreau", 0, "hawthorne", hawthorneChannels, 2);
    pb.connectInOrder ("pequod", 0, "hawthorne", hawthorneChannels + 2, 2);
    // the guitar: interface input 1 into PEQUOD Host
    pb.connectInOrder ("interface", 0, "pequod", 0, 1);
}

//==============================================================================
void Hub::initialiseAudio (const juce::XmlElement* savedState)
{
    deviceManager.initialise (interfaceChannels, interfaceChannels, savedState, true);
    deviceManager.addAudioCallback (this);
}

void Hub::prepare (double rate, int block, int numIn, int numOut)
{
    sampleRate = rate > 0 ? rate : 48000.0;
    blockSize = juce::jmax (16, block);
    sourcePool.setSize (juce::jmax (1, patchbay.getNumSources()), blockSize, false, true, false);
    sourcePool.clear();
    int maxDests = 1;
    for (auto& n : nodes) maxDests = juce::jmax (maxDests, n->port.dests.size());
    destScratch.setSize (maxDests, blockSize, false, true, false);
    destPtrs.resize ((size_t) maxDests);
    for (int c = 0; c < maxDests; ++c) destPtrs[(size_t) c] = destScratch.getWritePointer (c);
    sourcePtrs.resize ((size_t) sourcePool.getNumChannels());
    for (int c = 0; c < sourcePool.getNumChannels(); ++c) sourcePtrs[(size_t) c] = sourcePool.getWritePointer (c);
    juce::ignoreUnused (numIn, numOut);
    for (auto& n : nodes) n->prepare (sampleRate, blockSize);
}

void Hub::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    {
        const juce::SpinLock::ScopedLockType sl (structureLock);
        prepare (device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
                 device->getActiveInputChannels().countNumberOfSetBits(), device->getActiveOutputChannels().countNumberOfSetBits());
        running = true;
    }
    // every part restarts at the interface's rate and block
    for (auto& n : nodes)
        if (auto* p = dynamic_cast<PartNode*> (n.get()))
            if (auto* d = p->device.load()) d->interfaceRestarted();
}

void Hub::audioDeviceStopped()
{
    running = false;
    for (auto& n : nodes)
        if (auto* p = dynamic_cast<PartNode*> (n.get()))
            if (auto* d = p->device.load()) d->interfaceStopped();
}

//==============================================================================
void Hub::gather (const Matrix& m, int firstDest, int numDests, float* const* into, int n) noexcept
{
    for (int d = 0; d < numDests; ++d)
    {
        float* dst = into[d];
        const auto& cords = m.into[(size_t) (firstDest + d)];
        if (cords.empty()) { juce::FloatVectorOperations::clear (dst, n); continue; }
        bool first = true;
        for (auto& c : cords)
        {
            const float* src = sourcePtrs[(size_t) c.source];
            if (first)
            {
                if (c.gain == 1.0f) juce::FloatVectorOperations::copy (dst, src, n);
                else juce::FloatVectorOperations::copyWithMultiply (dst, src, c.gain, n);
                first = false;
            }
            else if (c.gain == 1.0f) juce::FloatVectorOperations::add (dst, src, n);
            else juce::FloatVectorOperations::addWithMultiply (dst, src, c.gain, n);
        }
    }
}

void Hub::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                            int n, const juce::AudioIODeviceCallbackContext&)
{
    const juce::SpinLock::ScopedTryLockType sl (structureLock);
    const auto* m = patchbay.getMatrix();
    if (! sl.isLocked() || m == nullptr || n > blockSize)
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
        return;
    }

    const auto t0 = juce::Time::getHighResolutionTicks();
    auto* iface = static_cast<InterfaceNode*> (interfaceNode);
    iface->in = in;   iface->numIn = numIn;
    iface->out = out; iface->numOut = numOut;

    for (auto& s : steps)
    {
        auto& p = s.node->port;
        const auto& pp = patchbay.getPorts()[(size_t) s.node->portIndex];
        float* const* sources = sourcePtrs.data() + pp.firstSource;
        switch (s.kind)
        {
            case Step::pull:
                s.node->pull (sources, n);
                break;
            case Step::push:
                gather (*m, pp.firstDest, p.dests.size(), destPtrs.data(), n);
                s.node->push (destPtrs.data(), n);
                break;
            case Step::process:
                gather (*m, pp.firstDest, p.dests.size(), destPtrs.data(), n);
                s.node->process (destPtrs.data(), sources, n);
                break;
        }
    }

    patchbay.audioBlockDone();
    const double took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    load.store ((float) (took * sampleRate / juce::jmax (1, n)), std::memory_order_relaxed);
}

//==============================================================================
void Hub::partDeviceOpened (PartDevice& d)
{
    const juce::SpinLock::ScopedLockType sl (structureLock);
    d.getPart().device.store (&d, std::memory_order_release);
}

void Hub::partDeviceClosed (PartDevice& d)
{
    const juce::SpinLock::ScopedLockType sl (structureLock);
    auto& part = d.getPart();
    PartDevice* expected = &d;
    part.device.compare_exchange_strong (expected, nullptr);
}

//==============================================================================
void Hub::updateVisibleTracks (int numTracks)
{
    visibleTracks = numTracks;
    constexpr int maxT = hawthorne::route::maxTracks;
    if (auto* e = findPart ("emerson"))
    {
        e->visibleIns.clear();
        e->visibleOuts.clear();
        for (int t = 0; t < numTracks; ++t) { e->visibleIns.push_back (t); e->visibleOuts.push_back (t); }
        e->visibleIns.insert (e->visibleIns.end(), { maxT, maxT + 1, maxT + 2 });   // master L / R, LTC
        e->visibleOuts.insert (e->visibleOuts.end(), { maxT, maxT + 1 });          // to master L / R
    }
    if (auto* w = findPart ("whitman"))
    {
        w->visibleIns.clear();
        for (int t = 0; t < numTracks; ++t) w->visibleIns.push_back (t);
        w->visibleIns.insert (w->visibleIns.end(), { maxT, maxT + 1, maxT + 2, maxT + 3 });   // returns A / B
    }
}

void Hub::reopenPart (PartNode& p)
{
    auto* mgr = p.manager;
    if (mgr == nullptr || mgr->getCurrentAudioDevice() == nullptr) return;
    auto setup = mgr->getAudioDeviceSetup();
    mgr->closeAudioDevice();
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.inputChannels.setRange (0, (int) p.visibleIns.size(), true);
    setup.outputChannels.setRange (0, (int) p.visibleOuts.size(), true);
    mgr->setAudioDeviceSetup (setup, true);
}

void Hub::timerCallback()
{
    patchbay.collectGarbage (running.load());

    // EMERSON and WHITMAN show the reel's tracks, like Hawthorne Route
    for (auto& n : nodes)
        if (auto* r = dynamic_cast<RouteBridge*> (n.get()))
        {
            const int t = r->getNumTracks();
            const int want = t > 0 ? t : hawthorne::route::defaultTracks;
            if (want != visibleTracks)
            {
                updateVisibleTracks (want);
                if (auto* e = findPart ("emerson")) reopenPart (*e);
                if (auto* w = findPart ("whitman")) reopenPart (*w);
            }
        }
}

} // namespace melville
