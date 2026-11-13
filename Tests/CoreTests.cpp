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
        CHECK (all.getDeviceNames().size() == 5 + Hub::maxIrvings);   // (Hawthorne, EMERSON, WHITMAN, THOREAU, ALCOTT and the IRVINGs)
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
    // (the fake Hawthorne plays its track 1 back on its device output: cable that to Out 1,
    //  where MELVILLE's click goes by default now)
    {
        auto& p = hub.getPatchbay();
        p.disconnect (p.findSource ("clock", "Click"), p.findDest ("interface", "Out 1"));
        p.disconnect (p.findSource ("twain", "Speakers A L"), p.findDest ("interface", "Out 1"));   // (TWAIN's monitor, by default)
        p.connect (p.findSource ("hawthorne", "Click"), p.findDest ("interface", "Out 1"));
    }

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

    // --- patchbay: no normals - the usual wiring is cables, all on the bay
    auto& pb = hub.getPatchbay();
    auto saved = pb.toXml();
    const int src = pb.findSource ("interface", "In 1"), src2 = pb.findSource ("interface", "In 2");
    const int dst = pb.findDest ("hawthorne", "Track 1"), dst2 = pb.findDest ("hawthorne", "Track 2");
    auto passes = [&] { hub.processBlockForTest (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, n);
                        return out.getSample (0, 5) == in.getSample (0, 5) && out.getMagnitude (0, 0, n) > 0.0f; };
    auto silent = [&] { hub.processBlockForTest (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, n);
                        return out.getMagnitude (0, 0, n) == 0.0f; };
    CHECK (pb.isConnected (src, dst));              // the usual wiring: a cable you can see
    CHECK (passes());
    const int click = pb.findSource ("hawthorne", "Click"), out1 = pb.findDest ("interface", "Out 1");
    pb.clearAll();                                  // PULL ALL: nothing behind the bay, so nothing at all
    CHECK (pb.getAllCords().empty());
    pb.connect (click, out1);                       // (back on the cable the test listens to)
    CHECK (silent());                               // (In 1 isn't on track 1 any more)
    pb.connect (src, dst);
    CHECK (passes());
    pb.connect (src2, dst);                         // a second cable mixes in (In 2 is silent here)
    CHECK (pb.isConnected (src, dst) && pb.isConnected (src2, dst) && passes());
    pb.disconnect (src, dst);
    CHECK (silent());
    pb.fromXml (*saved);                            // saving and loading brings the cables back
    CHECK (pb.isConnected (src, dst) && pb.getAllCords().size() == saved->getNumChildElements() && passes());

    // your usual wiring: kept, and put back (as cables)
    {
        auto before = pb.toXml();
        pb.connect (src2, dst2);
        auto usual = pb.wiringToXml();
        pb.clearAll();
        pb.wiringFromXml (*usual);
        CHECK (pb.isConnected (src2, dst2) && pb.isConnected (src, dst));
        pb.fromXml (*before);
    }

    // a patch from the days of normals (version 6): each normal that was carrying comes in as a
    // cable; one a cable had broken stays out, so it's connected exactly as it was
    {
        juce::XmlElement old ("PATCH");
        old.setAttribute ("version", 6);
        auto add = [&old] (const char* tag, const char* fp, const char* f, const char* tp, const char* t, bool full = false)
        {
            auto* e = old.createNewChildElement (tag);
            e->setAttribute ("fromPort", fp); e->setAttribute ("from", f); e->setAttribute ("toPort", tp); e->setAttribute ("to", t);
            if (full) e->setAttribute ("full", true);
        };
        add ("NORMAL", "interface", "In 1", "hawthorne", "Track 1");                 // carrying: becomes a cable
        add ("NORMAL", "interface", "In 1", "hawthorne", "Track 2");                 // broken by the cable below: stays out
        add ("CORD",   "interface", "In 2", "hawthorne", "Track 2");
        add ("NORMAL", "interface", "In 2", "hawthorne", "Track 3", true);           // full, its source plugged: stays out
        add ("CORD",   "hawthorne", "Click", "interface", "Out 1");
        pb.fromXml (old);
        CHECK (pb.isConnected (src, dst));
        CHECK (! pb.isConnected (src, dst2) && pb.isConnected (src2, dst2));
        CHECK (! pb.isConnected (src2, pb.findDest ("hawthorne", "Track 3")));
        CHECK (pb.getAllCords().size() == 3);
        CHECK (passes());
        CHECK (pb.toXml()->getNumChildElements() == 3 && pb.toXml()->getChildByName ("NORMAL") == nullptr);
        pb.fromXml (*saved);
    }

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

    // --- the MIDI patch bay: MIDI goes only to MIDI; THOREAU's steps reach only what they're cabled to,
    //     and let go a step later; MIDI clock is 24 a beat, with start
    {
        auto& mpb = hub.getPatchbay();
        auto* midi = hub.getMidi();
        CHECK (midi != nullptr);
        const int steps = mpb.findSource ("midi", "THOREAU steps"), clk = mpb.findSource ("midi", "Clock");
        const int slot3 = mpb.findDest ("midi", "ALCOTT slot 3"), slot4 = mpb.findDest ("midi", "ALCOTT slot 4");
        CHECK (steps >= 0 && clk >= 0 && slot3 >= 0 && slot4 >= 0);
        CHECK (mpb.isConnected (mpb.findSource ("midi", "Keyboards"), slot3));      // (the usual cable)
        const auto cordsBefore = mpb.getAllCords().size();
        mpb.connect (steps, mpb.findDest ("hawthorne", "Track 1"));                   // MIDI into audio: refused
        mpb.connect (mpb.findSource ("interface", "In 1"), slot4);                     // audio into MIDI: refused
        CHECK (mpb.getAllCords().size() == cordsBefore);
        mpb.connect (steps, slot3);

        const int block = 512;
        juce::AudioBuffer<float> silence (juce::jmax (1, midi->port.sources.size()), block);
        auto pullBlock = [&] { midi->pull (silence.getArrayOfWritePointers(), block); };
        pullBlock();
        midi->postStep (3, 38, 0.9f, 10);
        juce::MidiBuffer into3, into4;
        midi->take (slot3 - mpb.findPort ("midi")->firstDest, into3);
        midi->take (slot4 - mpb.findPort ("midi")->firstDest, into4);
        int ons = 0, at = -1, ch = 0;
        for (const auto meta : into3) if (meta.getMessage().isNoteOn()) { ++ons; at = meta.samplePosition; ch = meta.getMessage().getChannel(); }
        CHECK (ons == 1 && at == 10 && ch == 3);
        int ons4 = 0;
        for (const auto meta : into4) if (meta.getMessage().isNoteOn()) ++ons4;
        CHECK (ons4 == 0);
        // the note-off: a sixteenth at the tempo (120 by default) later, a little short of it
        int offAt = -1;
        for (int b = 1; b < 40 && offAt < 0; ++b)
        {
            pullBlock();
            juce::MidiBuffer m;
            midi->take (slot3 - mpb.findPort ("midi")->firstDest, m);
            for (const auto meta : m) if (meta.getMessage().isNoteOff()) offAt = b * block + meta.samplePosition;
        }
        CHECK (offAt > 4000 && offAt < 6000);
        std::printf ("    MIDI: THOREAU step on channel 3 at 10, its note-off %d samples on\n", offAt);

        // MIDI clock: one second rolling at 120 = 2 beats = 48 clocks, after a start
        auto& clock = hub.getClock();
        const auto oldState = clock.getState;
        clock.getState = [] { ClockNode::State st; st.valid = true; st.rolling = true; st.pos = 0.0; st.rate = 1.0; st.clockRate = 48000.0; st.bpm = 120.0; return st; };
        juce::AudioBuffer<float> clockOut (2, 48000);
        midi->pull (silence.getArrayOfWritePointers(), block);
        clock.prepare (48000.0, 48000);
        clock.pull (clockOut.getArrayOfWritePointers(), 48000);
        int ticks = 0, starts = 0;
        for (const auto meta : midi->source (midi->clock()))
        {
            const auto m = meta.getMessage();
            if (m.isMidiClock()) ++ticks;
            if (m.isMidiStart()) ++starts;
        }
        CHECK (starts == 1);
        CHECK (ticks == 47 || ticks == 48);
        std::printf ("    MIDI: clock %d ticks in a second at 120, %d start\n", ticks, starts);
        clock.getState = oldState;
    }

    std::printf (failures == 0 ? "MELVILLE core: all tests passed\n" : "MELVILLE core: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
