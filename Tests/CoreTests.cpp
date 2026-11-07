// MELVILLE core tests: the hub, part devices, patchbay and both bridges, with fake parts
// standing in for the apps. Each fake does what its app does at the device level:
//   Hawthorne: device in 1 (TRACK IN 1) -> device out 1; track 1 = device in 1, written to the Route
//              memory the way RouteHost::endBlock does
//   EMERSON:   "Hawthorne track 1" -> "To Hawthorne master L" at half level (its mix chain); also writes
//              track 1 into the link memory the way LinkHost::writeToWhitman does
//   WHITMAN:   "Hawthorne track 1" -> "To Hawthorne master L"

#include "../Source/Core/Hub.h"
#include "../Source/Core/PartDevice.h"
#include "../Source/Core/Bridges.h"
#include "../Source/Hosted/MelvilleHosted.h"
#include <cstdio>

using namespace melville;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

template <typename T> static T* findNode (Hub& hub)
{
    for (auto& n : hub.getNodes()) if (auto* t = dynamic_cast<T*> (n.get())) return t;
    return nullptr;
}

struct FakeCallback : juce::AudioIODeviceCallback
{
    std::function<void (const float* const*, int, float* const*, int, int)> fn;
    int started = 0, stopped = 0;
    double rate = 0; int block = 0; int numIn = 0, numOut = 0;
    void audioDeviceIOCallbackWithContext (const float* const* in, int ni, float* const* out, int no, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        numIn = ni; numOut = no;
        fn (in, ni, out, no, n);
    }
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { ++started; rate = d->getCurrentSampleRate(); block = d->getCurrentBufferSizeSamples(); }
    void audioDeviceStopped() override { ++stopped; }
};

static std::unique_ptr<juce::AudioIODevice> openPart (Hub& hub, const char* id)
{
    PartDeviceType type (hub, id);
    std::unique_ptr<juce::AudioIODevice> d (type.createDevice (type.getDeviceNames()[0], {}));
    juce::BigInteger ins, outs;
    ins.setRange (0, d->getInputChannelNames().size(), true);
    outs.setRange (0, d->getOutputChannelNames().size(), true);
    d->open (ins, outs, 0, 0);
    return d;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;   // message manager for the hub's timer
    constexpr int n = 64;

    Hub hub;
    hub.prepareForTest (48000.0, n);
    auto* route = findNode<RouteBridge> (hub);
    auto* link = findNode<LinkBridge> (hub);
    CHECK (route != nullptr && route->getShared() != nullptr);
    CHECK (link != nullptr && link->getShared() != nullptr);
    auto* rs = route->getShared();
    auto* ls = link->getShared();

    // --- parts' devices show the right channels
    {
        PartDeviceType all (hub);
        CHECK (all.getDeviceNames().size() == 4 + Hub::maxIrvings);   // (Hawthorne, EMERSON, WHITMAN, THOREAU and the IRVINGs)
        PartDeviceType one (hub, "emerson");
        CHECK (one.getDeviceNames() == juce::StringArray ("Hawthorne Route (MELVILLE)"));
    }

    // --- fake Hawthorne
    FakeCallback hawthorne;
    auto hawDev = openPart (hub, "hawthorne");
    hawthorne.fn = [&] (const float* const* in, int, float* const* out, int no, int nn)
    {
        for (int c = 0; c < no; ++c) juce::FloatVectorOperations::clear (out[c], nn);
        juce::FloatVectorOperations::copy (out[0], in[0], nn);
        // RouteHost::endBlock: track 1 = device in 1, everything else silent
        const auto w = rs->fromWrite.load();
        for (int ch = 0; ch < hawthorne::route::fromHawthorneChannels; ++ch)
            for (int k = 0; k < nn; ++k)
                rs->fromHawthorne[ch][(w + k) & hawthorne::route::ringMask] = ch == 0 ? in[0][k] : 0.0f;
        rs->fromWrite.store (w + nn);
        rs->clock.store (rs->clock.load() + nn);
    };
    hawDev->start (&hawthorne);
    CHECK (hawthorne.started == 1 && hawthorne.rate == 48000.0 && hawthorne.block == n);

    // --- fake EMERSON
    FakeCallback emerson;
    auto emDev = openPart (hub, "emerson");
    const auto emIns = emDev->getInputChannelNames(), emOuts = emDev->getOutputChannelNames();
    CHECK (emIns.size() == hawthorne::route::defaultTracks);        // just the reel tracks
    CHECK (emIns[0] == "Hawthorne track 1" && emOuts[0] == "To Hawthorne track 1");
    std::vector<float> emSaw (n);
    emerson.fn = [&] (const float* const* in, int, float* const* out, int no, int nn)
    {
        for (int c = 0; c < no; ++c) juce::FloatVectorOperations::clear (out[c], nn);
        juce::FloatVectorOperations::copyWithMultiply (out[hawthorne::route::defaultTracks], in[0], 0.5f, nn);   // master L
        std::copy (in[0], in[0] + nn, emSaw.begin());
        // LinkHost::writeToWhitman: track 1 into the link
        const auto w = ls->toWrite.load();
        for (int k = 0; k < nn; ++k) ls->toWhitman[0][(w + k) & emerson::link::ringMask] = in[0][k];
        ls->toWrite.store (w + nn);
    };
    emDev->start (&emerson);

    // --- fake WHITMAN
    FakeCallback whitman;
    auto whDev = openPart (hub, "whitman");
    CHECK (whDev->getOutputChannelNames()[0] == "To Hawthorne master L");
    std::vector<float> whSaw (n);
    whitman.fn = [&] (const float* const* in, int, float* const* out, int no, int nn)
    {
        for (int c = 0; c < no; ++c) juce::FloatVectorOperations::clear (out[c], nn);
        juce::FloatVectorOperations::copy (out[0], in[0], nn);
        std::copy (in[0], in[0] + nn, whSaw.begin());
    };
    whDev->start (&whitman);

    // --- run blocks: a ramp into interface input 1
    juce::AudioBuffer<float> in (2, n), out (2, n);
    for (int block = 0; block < 8; ++block)
    {
        for (int k = 0; k < n; ++k) in.setSample (0, k, (float) (block * n + k + 1));
        in.clear (1, 0, n);
        hub.processBlockForTest (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, n);

        // interface -> Hawthorne -> interface, same block, sample for sample
        bool same = true;
        for (int k = 0; k < n; ++k) same &= out.getSample (0, k) == in.getSample (0, k);
        CHECK (same);

        // Hawthorne's track 1 reaches EMERSON in the same block (no extra delay)
        bool em = true;
        for (int k = 0; k < n; ++k) em &= emSaw[(size_t) k] == in.getSample (0, k);
        CHECK (em);

        // and WHITMAN gets EMERSON's track 1 in the same block too
        bool wh = true;
        for (int k = 0; k < n; ++k) wh &= whSaw[(size_t) k] == in.getSample (0, k);
        CHECK (wh);

        // EMERSON's mix went into Hawthorne's ROUTE master L, at the frames just written
        const auto tw = rs->toWrite.load();
        CHECK (tw == (block + 1) * n);
        bool back = true;
        for (int k = 0; k < n; ++k)
            back &= rs->toHawthorne[hawthorne::route::maxTracks][(tw - n + k) & hawthorne::route::ringMask] == 0.5f * in.getSample (0, k);
        CHECK (back);

        // WHITMAN's mix went back over the link for exactly the block EMERSON wrote
        CHECK (ls->fromWrite.load() == ls->toWrite.load());
        bool mix = true;
        const auto fw = ls->fromWrite.load();
        for (int k = 0; k < n; ++k)
            mix &= ls->fromWhitman[0][(fw - n + k) & emerson::link::ringMask] == in.getSample (0, k);
        CHECK (mix);
    }
    CHECK (rs->driverRunning.load() == 1);
    CHECK (std::string (rs->clientName) == "MELVILLE");

    // --- patchbay: unpatching stops the signal; saving and loading brings it back
    auto& pb = hub.getPatchbay();
    auto saved = pb.toXml();
    const int src = pb.findSource ("interface", "In 1");
    const int dst = pb.findDest ("hawthorne", "Track 1");
    CHECK (pb.isConnected (src, dst));
    pb.disconnect (src, dst);
    hub.processBlockForTest (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, n);
    CHECK (out.getMagnitude (0, 0, n) == 0.0f);
    pb.fromXml (*saved);
    CHECK (pb.isConnected (src, dst));
    hub.processBlockForTest (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, n);
    CHECK (out.getSample (0, 5) == in.getSample (0, 5));

    // a mult with gain: interface in 1 also onto Hawthorne in 2 at -6 dB
    pb.connect (src, pb.findDest ("hawthorne", "Track 2"), 0.5f);
    CHECK (pb.getAllCords().size() == saved->getNumChildElements() + 1);

    // old matrices are freed once the audio thread has moved on
    pb.collectGarbage (true);

    // --- stopping a part silences its outputs
    emDev->stop();
    CHECK (emerson.stopped == 1);
    hub.processBlockForTest (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, n);
    const auto tw = rs->toWrite.load();
    float sum = 0;
    for (int k = 0; k < n; ++k) sum += std::abs (rs->toHawthorne[hawthorne::route::maxTracks][(tw - n + k) & hawthorne::route::ringMask]);
    CHECK (sum == 0.0f);

    whDev->close(); emDev->close(); hawDev->close();

    std::printf (failures == 0 ? "MELVILLE core: all tests passed\n" : "MELVILLE core: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
