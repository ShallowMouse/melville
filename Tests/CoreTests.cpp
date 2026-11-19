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
#include "../Source/Core/OscDevices.h"
#include "../Source/Core/AsioNode.h"
#include "../Poe/Source/PoeEngine.h"
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
        CHECK (all.getDeviceNames().size() == 6 + Hub::maxIrvings);   // (Hawthorne, EMERSON, WHITMAN, THOREAU, ALCOTT, POE and the IRVINGs)
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

    // --- POE: its jacks, its MIDI reaching what it's cabled to (ALCOTT slot 1 by default), and the sequencer itself
    {
        auto& mpb = hub.getPatchbay();
        auto* midi = hub.getMidi();
        const int poeOut = mpb.findSource ("midi", "POE"), poeIn = mpb.findDest ("midi", "POE transpose");
        const int slot1 = mpb.findDest ("midi", "ALCOTT slot 1"), slot2 = mpb.findDest ("midi", "ALCOTT slot 2");
        CHECK (poeOut >= 0 && poeIn >= 0 && slot1 >= 0);
        CHECK (mpb.isConnected (poeOut, slot1) && ! mpb.isConnected (poeOut, slot2));
        CHECK (mpb.findSource ("poe", "Pitch CV") >= 0 && mpb.findSource ("poe", "Gate") >= 0 && mpb.findSource ("poe", "CV C") >= 0);
        juce::AudioBuffer<float> silence (juce::jmax (1, midi->port.sources.size()), 512);
        midi->pull (silence.getArrayOfWritePointers(), 512);
        midi->postPoe (juce::MidiMessage::noteOn (1, 57, 0.8f), 100);
        juce::MidiBuffer s1, s2;
        midi->take (slot1 - mpb.findPort ("midi")->firstDest, s1);
        midi->take (slot2 - mpb.findPort ("midi")->firstDest, s2);
        int at = -1;
        for (const auto meta : s1) if (meta.getMessage().isNoteOn() && meta.getMessage().getNoteNumber() == 57) at = meta.samplePosition;
        int any2 = 0;
        for (const auto meta : s2) if (meta.getMessage().getNoteNumber() == 57) ++any2;
        CHECK (at == 100 && any2 == 0);

        // the sequencer, offline: 120 BPM, 1/16 = 6000 samples a step at 48 kHz
        poe::Engine e;
        e.prepare (48000.0);
        e.run = true;
        e.scale = 0; e.octave = 3; e.root = 0; e.range = 1;          // chromatic, from C3 (48), one octave
        auto& p = e.pattern (0);
        for (int i = 0; i < poe::Engine::numSteps; ++i) { p.steps[(size_t) i].a = (float) i / 12.0f; p.steps[(size_t) i].c = 0.5f; }
        struct Ev { juce::int64 at; int note; bool on; };
        auto runFor = [&] (int samples, double startTape, std::vector<Ev>& evs, std::vector<float>* pitch = nullptr, std::vector<float>* gate = nullptr,
                           bool rolling = true)
        {
            juce::AudioBuffer<float> out (poe::Engine::numOutputs, 512);
            juce::MidiBuffer none, m;
            for (int done = 0, n = 512; done < samples; done += n)
            {
                n = juce::jmin (512, samples - done);     // (exactly `samples`)
                poe::Engine::Clock c;
                c.running = rolling; c.tapePos = startTape + done; c.tapePerSample = 1.0; c.bpm = 120.0; c.reelRate = 48000.0;
                out.clear(); m.clear();
                e.process (out.getArrayOfWritePointers(), out.getNumChannels(), n, c, none, m);
                for (const auto meta : m)
                {
                    const auto msg = meta.getMessage();
                    if (msg.isNoteOnOrOff()) evs.push_back ({ done + meta.samplePosition, msg.getNoteNumber(), msg.isNoteOn() });
                }
                if (pitch) for (int i = 0; i < n; ++i) pitch->push_back (out.getSample (poe::Engine::pitchCv, i));
                if (gate) for (int i = 0; i < n; ++i) gate->push_back (out.getSample (poe::Engine::gateOut, i));
            }
        };
        std::vector<Ev> ev;
        std::vector<float> pitch, gate;
        runFor (4 * 6000, 0.0, ev, &pitch, &gate);
        std::vector<Ev> ons;
        for (auto& x : ev) if (x.on) ons.push_back (x);
        CHECK (ons.size() == 4);
        CHECK (ons.size() == 4 && ons[0].at == 0 && ons[1].at == 6000 && ons[2].at == 12000 && ons[3].at == 18000);
        CHECK (ons.size() == 4 && ons[0].note == 48 && ons[1].note == 49 && ons[3].note == 51);
        // gate time 0.5 holds 52.5 % of the step; the CV: 1 V an octave from C2 at 10 V full scale (C3 = 1 V = 0.1)
        juce::int64 firstOff = -1;
        for (auto& x : ev) if (! x.on) { firstOff = x.at; break; }
        CHECK (firstOff == 3150);
        CHECK (std::abs (pitch[100] - 0.1f) < 1e-5f && std::abs (pitch[6100] - (13.0f / 12.0f) / 10.0f) < 1e-5f);
        CHECK (gate[100] > 0.49f && gate[100] < 0.51f && gate[4000] == 0.0f);

        // a TIE: step 2 carries straight on from step 1 (step 1's note holds; no new gate)
        p.steps[1].mode = poe::Engine::tie;
        ev.clear(); gate.clear();
        runFor (2 * 6000, 0.0, ev, nullptr, &gate);
        int ons2 = 0;
        juce::int64 off48 = -1;
        for (auto& x : ev) { if (x.on) ++ons2; if (! x.on && x.note == 48) off48 = x.at; }
        CHECK (ons2 == 2 && off48 == 6000);                                // (49 on, then 48 off, at the tie)
        CHECK (gate[5990] > 0.4f && gate[6010] > 0.4f);                    // the gate holds through
        p.steps[1].mode = poe::Engine::play;

        // LENGTH 4 with step 2 skipped: steps 1, 3, 4, 1 ...; and PENDULUM over four steps
        p.length = 4;
        p.steps[1].mode = poe::Engine::skip;
        CHECK (e.stepAt (0) == 0 && e.stepAt (1) == 2 && e.stepAt (2) == 3 && e.stepAt (3) == 0);
        p.steps[1].mode = poe::Engine::play;
        e.direction = poe::Engine::pendulum;
        CHECK (e.stepAt (3) == 3 && e.stepAt (4) == 2 && e.stepAt (6) == 0 && e.stepAt (7) == 1);
        e.direction = poe::Engine::randomly;
        CHECK (e.stepAt (123) == e.stepAt (123));                           // (the same at the same place)
        e.direction = poe::Engine::forward;
        p.length = poe::Engine::numSteps;

        // SWING at 75 %: the second step of each pair comes at 1.5 steps
        e.swing = 0.5f;
        ev.clear();
        runFor (2 * 6000, 0.0, ev);
        juce::int64 second = -1;
        for (auto& x : ev) if (x.on && x.note == 49) second = x.at;
        CHECK (second == 9000);
        e.swing = 0.0f;

        // following the tape: started mid-way into step 6 it plays step 7 next; stopping lets the note go
        ev.clear();
        runFor (6000, 6.5 * 6000.0, ev);
        juce::int64 at54 = -1;
        for (auto& x : ev) if (x.on && x.note == 54 && at54 < 0) at54 = x.at;
        CHECK (at54 == 0);                                                  // (the step it's in, at once)
        bool next = false;
        for (auto& x : ev) if (x.on && x.note == 55 && x.at == 3000) next = true;
        CHECK (next);
        ev.clear();
        runFor (512, 0.0, ev, nullptr, nullptr, false);
        CHECK (ev.size() == 1 && ! ev[0].on);

        // and it keeps in the session
        e.pattern (3).steps[5].a = 0.25f; e.pattern (3).length = 7; e.rate = 4;
        poe::Engine f;
        f.fromXml (*e.toXml());
        CHECK (f.pattern (3).steps[5].a.load() == 0.25f && f.pattern (3).length.load() == 7 && f.rate.load() == 4 && f.run.load());
        std::printf ("    POE: steps every 6000 samples at 120, gates, ties, skips, swing, the tape's place and the session\n");
    }

    // --- control surfaces: a device is sent only its own pages, and presses only its own keys
    {
        namespace Osc = melville;
        CHECK (Osc::typeWants (Osc::typeHawthorne, "/hawthorne/play"));
        CHECK (Osc::typeWants (Osc::typeHawthorne, "/transport/rec"));        // (the deck runs MELVILLE's clock with no reel)
        CHECK (! Osc::typeWants (Osc::typeHawthorne, "/twain/mute"));         // a transport can't touch the monitoring
        CHECK (Osc::typeWants (Osc::typeTwain, "/twain/level"));
        CHECK (! Osc::typeWants (Osc::typeTwain, "/hawthorne/rec"));          // ... and the monitoring can't hit record
        CHECK (Osc::typeWants (Osc::typeEverything, "/twain/mute") && Osc::typeWants (Osc::typeEverything, "/hawthorne/rec"));
        for (int t = 0; t < Osc::numDeviceTypes; ++t)
            CHECK (Osc::typeWants (t, "/melville/sync"));                     // everyone may ask to be caught up
        CHECK (Osc::typeOf ("/hawthorne/mark") == Osc::typeHawthorne);
        CHECK (Osc::typeOf ("/twain/dim") == Osc::typeTwain);
        CHECK (Osc::typeOf ("/transport/play") == Osc::typeEverything);

        // the list is kept with the settings
        Osc::OscSettings s;
        s.devices.push_back ({ Osc::typeHawthorne, "Deck", "192.168.1.20", 0, "a1b2" });
        s.devices.push_back ({ Osc::typeTwain, "Control room", "192.168.1.21", 9200, {} });
        const auto back = Osc::OscSettings::fromXml (s.toXml().get());
        CHECK (back.devices.size() == 2);
        CHECK (back.devices[0].type == Osc::typeHawthorne && back.devices[0].name == "Deck"
               && back.devices[0].host == "192.168.1.20" && back.devices[0].serial == "a1b2");
        CHECK (back.devices[1].type == Osc::typeTwain && back.devices[1].replyPort == 9200);

        // a setting from before the device list: the addresses it answered become devices
        Osc::OscSettings old2;
        old2.replyHost = "192.168.1.30, 192.168.1.31";
        const auto moved = Osc::OscSettings::fromXml (old2.toXml().get());
        CHECK (moved.devices.size() == 2 && moved.devices[0].host == "192.168.1.30"
               && moved.devices[1].type == Osc::typeEverything);
        std::printf ("    OSC: %d devices kept, %d brought over from the old settings\n",
                     (int) back.devices.size(), (int) moved.devices.size());

        // the hardware: its pairing key is kept, and the firmware MELVILLE carries is the newest one there
        Osc::OscSettings paired;
        paired.devices.push_back ({ Osc::typeHawthorne, "Deck", "192.168.1.20", 0, "hawthorne-3a7f", "0123456789abcdef0123456789abcdef" });
        CHECK (Osc::OscSettings::fromXml (paired.toXml().get()).devices[0].key == "0123456789abcdef0123456789abcdef");
        CHECK (Osc::versionOlder ("3.1.0", "3.1.1") && ! Osc::versionOlder ("3.1.1", "3.1.0"));
        CHECK (Osc::versionOlder ("3.1.9", "3.1.10"));                 // (number by number, not as text)
        CHECK (Osc::versionOlder ("3.1", "3.1.1") && ! Osc::versionOlder ("3.1.0", "3.1"));
        CHECK (Osc::versionOlder ("3.1.0-dev", "3.1.0") && ! Osc::versionOlder ("3.1.0", "3.1.0"));
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("melville-fw-test");
        folder.deleteRecursively();
        folder.createDirectory();
        for (auto* n : { "HAWTHORNE-3.1.0.bin", "HAWTHORNE-3.1.10.bin", "HAWTHORNE-3.1.9.bin", "TWAIN-9.0.0.bin", "notes.txt" })
            folder.getChildFile (n).replaceWithText ("x");
        const auto fw = Osc::bundledFirmware (folder, Osc::typeHawthorne);
        CHECK (fw.version == "3.1.10" && fw.file.getFileName() == "HAWTHORNE-3.1.10.bin");
        CHECK (Osc::bundledFirmware (folder, Osc::typeTwain).version == "9.0.0");
        CHECK (Osc::bundledFirmware (folder.getChildFile ("none"), Osc::typeHawthorne).version.isEmpty());
        folder.deleteRecursively();
        std::printf ("    OSC: firmware %s is the newest MELVILLE would carry\n", fw.version.toRawUTF8());

        // a box's network settings: what's sendable, what goes, and reading what the box reports
        Osc::BoxNetwork n;
        CHECK (n.problem().isEmpty());                                       // (DHCP, found by itself)
        n.fixedIp = true; n.ip = "192.168.1.50";
        CHECK (n.problem().isEmpty());
        n.ip = "192.168.1.300";  CHECK (n.problem().contains ("address"));
        n.ip = "192.168.1";      CHECK (n.problem().contains ("address"));
        n.ip = "192.168.1.50"; n.mask = "255.0.255.0"; CHECK (n.problem().contains ("mask"));
        n.mask = "255.255.254.0"; n.gateway = "192.168.1.x"; CHECK (n.problem().contains ("gateway"));
        n.gateway = ""; n.fixedMelville = true; n.melvilleIp = ""; CHECK (n.problem().contains ("MELVILLE"));
        n.melvilleIp = "10.0.0.5"; n.backlight = false;
        CHECK (n.problem().isEmpty());
        CHECK (n.toStrings() == juce::StringArray ({ "fixed", "192.168.1.50", "255.255.254.0", "", "", "fixed", "10.0.0.5", "off" }));
        Osc::BoxNetwork r;
        CHECK (! Osc::BoxNetwork::fromReport ({ "hawthorne-3a7f", "dhcp" }, r));
        CHECK (Osc::BoxNetwork::fromReport ({ "hawthorne-3a7f", "dhcp", "192.168.1.42", "255.255.255.0", "192.168.1.1", "192.168.1.1",
                                              "", "", "", "", "auto", "", "192.168.1.10", "on", "3.2.0" }, r));
        CHECK (! r.fixedIp && r.nowIp == "192.168.1.42" && r.ip == "192.168.1.42" && r.gateway == "192.168.1.1");   // (a start for fixing it)
        CHECK (! r.fixedMelville && r.melvilleIp == "192.168.1.10" && r.backlight && r.firmware == "3.2.0");
        std::printf ("    OSC: a box's network settings check out before they go\n");
    }

    // --- MELVILLE ASIO: a program on the virtual driver gets 24 channels each way, on MELVILLE's clock
    {
        auto* panel = findNode<AsioNode> (hub);
        CHECK (panel != nullptr && panel->port.sources.size() == 24 && panel->port.dests.size() == 24);
        CHECK (panel != nullptr && panel->port.sources[0] == "Out 1" && panel->port.dests[23] == "In 24");

        AsioNode a;
        a.prepare (48000.0, n);
        auto* sh = a.getShared();
        CHECK (sh != nullptr && sh->sampleRate.load() == 48000 && sh->magic == asio::magic);
        std::vector<std::vector<float>> src (24, std::vector<float> ((size_t) n, 9.0f)), dst (24, std::vector<float> ((size_t) n, 0.0f));
        std::vector<float*> srcPtrs;  for (auto& v : src) srcPtrs.push_back (v.data());
        std::vector<const float*> dstPtrs; for (auto& v : dst) dstPtrs.push_back (v.data());

        a.pull (srcPtrs.data(), n);                                  // nobody on the driver: silence
        CHECK (src[3][0] == 0.0f && ! a.isConnected());

        // "REAPER" has played 1024 frames at a 128 buffer, a ramp on its output 4
        sh->driverRunning = 1; sh->driverBlock = 128; sh->driverClientId = 1;
        std::strcpy (sh->clientName, "reaper");
        for (int k = 0; k < 1024; ++k) sh->toMelville[3][k] = (float) k;
        sh->toWrite = 1024;
        a.pull (srcPtrs.data(), n);
        const int target = 128 + n + 32;
        CHECK (src[3][0] == (float) (1024 - target) && src[3][n - 1] == (float) (1024 - target + n - 1));
        CHECK (src[2][0] == 0.0f && a.getLatency() == target && a.isConnected() && a.clientName() == "reaper");
        a.pull (srcPtrs.data(), n);                                  // the next block carries straight on
        CHECK (src[3][0] == (float) (1024 - target + n));

        // MELVILLE's In 6 goes to the program's input 6, and the clock moves on
        // (on Windows this is the same named memory as the hub's own panel, which has run blocks already: go by where it stands)
        std::fill (dst[5].begin(), dst[5].end(), 0.5f);
        const auto clock0 = sh->clock.load(), w0 = sh->fromWrite.load();
        a.push (dstPtrs.data(), n);
        CHECK (sh->fromMelville[5][w0 & asio::ringMask] == 0.5f && sh->fromMelville[5][(w0 + n - 1) & asio::ringMask] == 0.5f
               && sh->fromMelville[4][w0 & asio::ringMask] == 0.0f);
        CHECK (sh->clock.load() == clock0 + n && sh->fromWrite.load() == w0 + n);
        sh->driverRunning = 0;                                      // ("REAPER" goes)
        a.release();
        CHECK (sh->sampleRate.load() == 0);
        std::printf ("    MELVILLE ASIO: 24 each way, the program's outputs %d samples behind, MELVILLE's clock drives it\n", target);
    }

    std::printf (failures == 0 ? "MELVILLE core: all tests passed\n" : "MELVILLE core: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
