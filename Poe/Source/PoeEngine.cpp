#include "PoeEngine.h"
#include <MelvilleHosted.h>

namespace poe
{

namespace
{
    // the scales knob A is quantised to (semitones above the root)
    struct Scale { const char* name; std::vector<int> notes; };
    const std::vector<Scale>& scales()
    {
        static const std::vector<Scale> s {
            { "Chromatic",      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { "Major",          { 0, 2, 4, 5, 7, 9, 11 } },
            { "Minor",          { 0, 2, 3, 5, 7, 8, 10 } },
            { "Dorian",         { 0, 2, 3, 5, 7, 9, 10 } },
            { "Mixolydian",     { 0, 2, 4, 5, 7, 9, 10 } },
            { "Phrygian",       { 0, 1, 3, 5, 7, 8, 10 } },
            { "Harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
            { "Major pentatonic", { 0, 2, 4, 7, 9 } },
            { "Minor pentatonic", { 0, 3, 5, 7, 10 } },
            { "Blues",          { 0, 3, 5, 6, 7, 10 } },
            { "Whole tone",     { 0, 2, 4, 6, 8, 10 } },
            { "Octaves",        { 0 } },
        };
        return s;
    }

    constexpr double rateSteps[] = { 1.0, 2.0, 3.0, 4.0, 6.0, 8.0 };     // steps a beat

    // a fixed scramble of the step count, so RANDOM plays the same at the same place on tape
    inline juce::uint32 hash (juce::int64 x) noexcept
    {
        auto v = (juce::uint64) x * 0x9E3779B97F4A7C15ull;
        v ^= v >> 31; v *= 0xBF58476D1CE4E5B9ull; v ^= v >> 29;
        return (juce::uint32) (v >> 16);
    }

    // where step `count` starts, in steps, with the swing (each pair's second step comes late)
    inline double stepStart (juce::int64 count, double swing) noexcept
    {
        const auto pair = (count >= 0 ? count : count - 1) / 2;
        const bool odd = (count - pair * 2) != 0;
        return (double) (pair * 2) + (odd ? 1.0 + swing : 0.0);
    }
    inline juce::int64 countAt (double u, double swing) noexcept
    {
        const double pairF = std::floor (u * 0.5);
        const double within = u - pairF * 2.0;
        return (juce::int64) pairF * 2 + (within >= 1.0 + swing ? 1 : 0);
    }
}

juce::StringArray Engine::rateNames()      { return { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }; }
juce::StringArray Engine::directionNames() { return { "Forward", "Backward", "Pendulum", "Random" }; }
juce::StringArray Engine::modeNames()      { return { "PLAY", "TIE", "REST", "SKIP" }; }
juce::StringArray Engine::scaleNames()
{
    juce::StringArray s;
    for (auto& x : scales()) s.add (x.name);
    return s;
}
double Engine::stepsPerBeat (int r) noexcept { return rateSteps[juce::jlimit (0, 5, r)]; }

juce::String Engine::noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    n = juce::jlimit (0, 127, n);
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
}

int Engine::noteFor (float a) const noexcept
{
    const auto& sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scale.load (std::memory_order_relaxed))].notes;
    const int per = (int) sc.size();
    const int degrees = per * juce::jlimit (1, 4, range.load (std::memory_order_relaxed)) + 1;
    const int idx = juce::roundToInt (juce::jlimit (0.0f, 1.0f, a) * (float) (degrees - 1));
    const int base = (octave.load (std::memory_order_relaxed) + 1) * 12 + root.load (std::memory_order_relaxed);
    return juce::jlimit (0, 127, base + 12 * (idx / per) + sc[(size_t) (idx % per)]);
}

int Engine::stepAt (juce::int64 count) const noexcept
{
    const auto& p = pattern (current.load (std::memory_order_relaxed));
    std::array<int, numSteps> playable {};
    int m = 0;
    const int len = juce::jlimit (1, numSteps, p.length.load (std::memory_order_relaxed));
    for (int i = 0; i < len; ++i)
        if (p.steps[(size_t) i].mode.load (std::memory_order_relaxed) != skip) playable[(size_t) m++] = i;
    if (m == 0) return -1;
    auto wrap = [] (juce::int64 x, juce::int64 by) { return (int) (((x % by) + by) % by); };
    switch (direction.load (std::memory_order_relaxed))
    {
        case backward: return playable[(size_t) (m - 1 - wrap (count, m))];
        case pendulum:
        {
            if (m == 1) return playable[0];
            const int k = wrap (count, 2 * m - 2);
            return playable[(size_t) (k < m ? k : 2 * m - 2 - k)];
        }
        case randomly: return playable[(size_t) (hash (count) % (juce::uint32) m)];
        default:       return playable[(size_t) wrap (count, m)];
    }
}

//==============================================================================
void Engine::release (juce::MidiBuffer& midi, int offset) noexcept
{
    if (noteOn >= 0) midi.addEvent (juce::MidiMessage::noteOff (noteChannel, noteOn), offset);
    noteOn = -1;
    soundingNote.store (-1, std::memory_order_relaxed);
    gateUp = false;
    holdOn = false;
}

void Engine::startStep (juce::int64 count, double startU, double lenU, int offset, juce::MidiBuffer& midi) noexcept
{
    const int i = stepAt (count);
    lit.store (i, std::memory_order_relaxed);
    if (i < 0) { release (midi, offset); return; }

    auto& p = now();
    const auto& s = p.steps[(size_t) i];
    const int mode = s.mode.load (std::memory_order_relaxed);
    const float a = s.a.load (std::memory_order_relaxed), b = s.b.load (std::memory_order_relaxed), c = s.c.load (std::memory_order_relaxed);
    const int ch = juce::jlimit (1, 16, channel.load (std::memory_order_relaxed));
    const int ccB = bCc.load (std::memory_order_relaxed), ccC = cCc.load (std::memory_order_relaxed);

    // rows B and C go out as CV whatever the step does (as on the 960, the rows always read)
    cvRowB = b;
    cvRowC = c;
    if (ccB >= 0) midi.addEvent (juce::MidiMessage::controllerEvent (ch, ccB, juce::roundToInt (b * 127.0f)), offset);
    if (ccC >= 0) midi.addEvent (juce::MidiMessage::controllerEvent (ch, ccC, juce::roundToInt (c * 127.0f)), offset);

    if (mode == rest) { release (midi, offset); return; }

    const int note = juce::jlimit (0, 127, noteFor (a) + (follows.load (std::memory_order_relaxed) ? transpose.load (std::memory_order_relaxed) : 0));
    const float vel = ccB >= 0 ? 0.8f : juce::jlimit (0.05f, 1.0f, b);
    const float gate = ccC >= 0 ? 0.5f : juce::jmap (c, 0.05f, 1.0f);
    cvTarget = (double) (note - pitchCvZero) / 12.0;

    // does the step after this one tie on? then the note holds to its end
    const int next = stepAt (count + 1);
    holdOn = next >= 0 && p.steps[(size_t) next].mode.load (std::memory_order_relaxed) == tie;
    gateEndU = startU + lenU * (holdOn ? 1.0 : (double) gate);

    if (mode == tie && noteOn >= 0)
    {
        // legato: the new note before the old one lets go; no new gate or trigger
        if (note != noteOn || ch != noteChannel)
        {
            midi.addEvent (juce::MidiMessage::noteOn (ch, note, vel), offset);
            midi.addEvent (juce::MidiMessage::noteOff (noteChannel, noteOn), offset);
        }
        noteOn = note; noteChannel = ch;
        gateUp = true;
    }
    else
    {
        if (noteOn >= 0) midi.addEvent (juce::MidiMessage::noteOff (noteChannel, noteOn), offset);
        midi.addEvent (juce::MidiMessage::noteOn (ch, note, vel), offset);
        noteOn = note; noteChannel = ch;
        if (gateUp) gapLeft = juce::jmax (1, (int) (sampleRate * 0.002));      // (a gap, so the envelope retriggers)
        gateUp = true;
        trigLeft = juce::jmax (1, (int) (sampleRate * 0.001));
    }
    soundingNote.store (note, std::memory_order_relaxed);
    if (glideMs.load (std::memory_order_relaxed) <= 0.0f) cvPitch = cvTarget;
}

void Engine::process (float* const* out, int numOut, int n, const Clock& clk, const juce::MidiBuffer& transposeIn,
                      juce::MidiBuffer& midi) noexcept
{
    // the TRANSPOSE jack: the latest note held (middle C plays as written)
    for (const auto meta : transposeIn)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) transpose.store (juce::jlimit (-48, 48, m.getNoteNumber() - 60), std::memory_order_relaxed);
    }

    const double spb = stepsPerBeat (rate.load (std::memory_order_relaxed));
    const double sw = juce::jlimit (0.0, 0.5, (double) swing.load (std::memory_order_relaxed));
    const double rateSr = juce::jmax (1000.0, clk.reelRate);
    const double uPerTape = juce::jmax (1.0, clk.bpm) / 60.0 / rateSr * spb;     // steps per tape sample
    const float fs = juce::jmax (0.1f, fullScale.load (std::memory_order_relaxed));
    const float gateV = juce::jmin (1.0f, 5.0f / fs), rowScale = 5.0f / fs;
    const float glide = glideMs.load (std::memory_order_relaxed);
    const double slew = glide > 0.0f ? 1.0 - std::exp (-1.0 / (sampleRate * glide * 0.001)) : 1.0;

    auto put = [&] (int ch, int i, float v) { if (ch < numOut && out[ch] != nullptr) out[ch][i] = v; };
    const bool go = clk.running && run.load (std::memory_order_relaxed);

    if (! go)
    {
        if (running) { release (midi, 0); lit.store (-1, std::memory_order_relaxed); }
        running = false;
    }

    for (int i = 0; i < n; ++i)
    {
        if (go)
        {
            const double tape = clk.tapePos + clk.tapePerSample * (double) i;
            const double u = (tape - clk.barOne) * uPerTape;
            const auto count = countAt (u, sw);
            if (! running || count != lastCount)
            {
                running = true;
                lastCount = count;
                const double start = stepStart (count, sw);
                startStep (count, start, stepStart (count + 1, sw) - start, i, midi);
            }
            else if (gateUp && ! holdOn && u >= gateEndU)
                release (midi, i);
        }

        cvPitch += (cvTarget - cvPitch) * slew;
        put (pitchCv, i, (float) juce::jlimit (-1.0, 1.0, cvPitch / fs));
        bool g = gateUp;
        if (gapLeft > 0) { --gapLeft; g = false; }
        put (gateOut, i, g ? gateV : 0.0f);
        put (trigOut, i, trigLeft > 0 ? gateV : 0.0f);
        if (trigLeft > 0) --trigLeft;
        put (cvB, i, juce::jlimit (0.0f, 1.0f, cvRowB * rowScale));
        put (cvC, i, juce::jlimit (0.0f, 1.0f, cvRowC * rowScale));
    }
}

//==============================================================================
void Rack::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    if (d != nullptr)
        for (auto& e : seqs) e.prepare (d->getCurrentSampleRate());
    transposeBlock.ensureSize (1024);
    midiBlock.ensureSize (1024);
}

void Rack::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                             const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    Engine::Clock clk;
    double pos = 0.0, perSample = 0.0;
    int flags = 0;
    const auto frame = melville::currentFrame();
    if (melville::tapePositionAt (frame, pos, perSample, flags) && (flags & 1) != 0 && perSample > 0.0)
    {
        auto& t = melville::transport();
        clk.running = true;
        clk.tapePos = pos;
        clk.tapePerSample = perSample;
        clk.bpm = t.bpm.load (std::memory_order_relaxed);
        clk.barOne = t.barOne.load (std::memory_order_relaxed);
        clk.reelRate = t.reelRate.load (std::memory_order_relaxed);
    }

    transposeBlock.clear();
    melville::takeMidi (transposeJack, transposeBlock);
    for (int i = 0; i < numSequencers; ++i)
    {
        // each sequencer's five CV outs, in turn
        const int first = i * Engine::numOutputs;
        float* const* mine = first < numOut ? out + first : nullptr;
        midiBlock.clear();
        seqs[(size_t) i].process (mine, mine != nullptr ? juce::jmin ((int) Engine::numOutputs, numOut - first) : 0, n, clk,
                                  seqs[(size_t) i].follows.load (std::memory_order_relaxed) ? transposeBlock : none, midiBlock);
        for (const auto meta : midiBlock)
            melville::postPoe (i, meta.getMessage(), meta.samplePosition);
    }
}

std::unique_ptr<juce::XmlElement> Rack::toXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("POE");
    for (int i = 0; i < numSequencers; ++i)
    {
        auto s = seqs[(size_t) i].toXml();
        s->setAttribute ("index", i);
        x->addChildElement (s.release());
    }
    return x;
}

void Rack::fromXml (const juce::XmlElement& x)
{
    const juce::XmlElement blank ("SEQUENCER");
    for (auto& e : seqs) e.fromXml (blank);
    if (x.getChildByName ("PATTERN") != nullptr) { seqs[0].fromXml (x); return; }     // (POE with one sequencer)
    for (auto* s : x.getChildWithTagNameIterator ("SEQUENCER"))
        seq (s->getIntAttribute ("index", 0)).fromXml (*s);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> Engine::toXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("SEQUENCER");
    x->setAttribute ("run", run.load());
    x->setAttribute ("pattern", current.load());
    x->setAttribute ("rate", rate.load());
    x->setAttribute ("direction", direction.load());
    x->setAttribute ("swing", (double) swing.load());
    x->setAttribute ("scale", scale.load());
    x->setAttribute ("root", root.load());
    x->setAttribute ("octave", octave.load());
    x->setAttribute ("range", range.load());
    x->setAttribute ("glide", (double) glideMs.load());
    x->setAttribute ("channel", channel.load());
    x->setAttribute ("ccB", bCc.load());
    x->setAttribute ("ccC", cCc.load());
    x->setAttribute ("fullScale", (double) fullScale.load());
    x->setAttribute ("follows", follows.load());
    for (int k = 0; k < numPatterns; ++k)
    {
        const auto& p = pattern (k);
        auto* px = x->createNewChildElement ("PATTERN");
        px->setAttribute ("length", p.length.load());
        for (auto& s : p.steps)
        {
            auto* sx = px->createNewChildElement ("STEP");
            sx->setAttribute ("a", (double) s.a.load());
            sx->setAttribute ("b", (double) s.b.load());
            sx->setAttribute ("c", (double) s.c.load());
            sx->setAttribute ("mode", s.mode.load());
        }
    }
    return x;
}

void Engine::fromXml (const juce::XmlElement& x)
{
    run = x.getBoolAttribute ("run", false);
    current = juce::jlimit (0, numPatterns - 1, x.getIntAttribute ("pattern", 0));
    rate = juce::jlimit (0, 5, x.getIntAttribute ("rate", 3));
    direction = juce::jlimit (0, 3, x.getIntAttribute ("direction", forward));
    swing = (float) juce::jlimit (0.0, 0.5, x.getDoubleAttribute ("swing", 0.0));
    scale = juce::jlimit (0, (int) scales().size() - 1, x.getIntAttribute ("scale", 1));
    root = juce::jlimit (0, 11, x.getIntAttribute ("root", 0));
    octave = juce::jlimit (0, 7, x.getIntAttribute ("octave", 3));
    range = juce::jlimit (1, 4, x.getIntAttribute ("range", 2));
    glideMs = (float) juce::jlimit (0.0, 2000.0, x.getDoubleAttribute ("glide", 0.0));
    channel = juce::jlimit (1, 16, x.getIntAttribute ("channel", 1));
    bCc = juce::jlimit (-1, 127, x.getIntAttribute ("ccB", -1));
    cCc = juce::jlimit (-1, 127, x.getIntAttribute ("ccC", -1));
    fullScale = (float) juce::jlimit (1.0, 24.0, x.getDoubleAttribute ("fullScale", 10.0));
    follows = x.getBoolAttribute ("follows", true);
    int k = 0;
    for (int i = 0; i < numPatterns; ++i) clearPattern (i);
    for (auto* px : x.getChildWithTagNameIterator ("PATTERN"))
    {
        if (k >= numPatterns) break;
        auto& p = pattern (k++);
        p.length = juce::jlimit (1, numSteps, px->getIntAttribute ("length", numSteps));
        int i = 0;
        for (auto* sx : px->getChildWithTagNameIterator ("STEP"))
        {
            if (i >= numSteps) break;
            auto& s = p.steps[(size_t) i++];
            s.a = (float) juce::jlimit (0.0, 1.0, sx->getDoubleAttribute ("a", 0.0));
            s.b = (float) juce::jlimit (0.0, 1.0, sx->getDoubleAttribute ("b", 0.75));
            s.c = (float) juce::jlimit (0.0, 1.0, sx->getDoubleAttribute ("c", 0.5));
            s.mode = juce::jlimit (0, 3, sx->getIntAttribute ("mode", play));
        }
    }
}

void Engine::clearPattern (int i)
{
    auto& p = pattern (i);
    p.length = numSteps;
    for (auto& s : p.steps) { s.a = 0.0f; s.b = 0.75f; s.c = 0.5f; s.mode = play; }
}

void Engine::copyPattern (int from, int to)
{
    if (from == to) return;
    auto& a = pattern (from);
    auto& b = pattern (to);
    b.length = a.length.load();
    for (int i = 0; i < numSteps; ++i)
    {
        b.steps[(size_t) i].a = a.steps[(size_t) i].a.load();
        b.steps[(size_t) i].b = a.steps[(size_t) i].b.load();
        b.steps[(size_t) i].c = a.steps[(size_t) i].c.load();
        b.steps[(size_t) i].mode = a.steps[(size_t) i].mode.load();
    }
}

void Engine::randomise (int i, bool pitchOnly)
{
    auto& r = juce::Random::getSystemRandom();
    auto& p = pattern (i);
    for (auto& s : p.steps)
    {
        s.a = r.nextFloat();
        if (pitchOnly) continue;
        s.b = 0.45f + 0.55f * r.nextFloat();
        s.c = 0.2f + 0.7f * r.nextFloat();
        const float m = r.nextFloat();
        s.mode = m < 0.72f ? play : m < 0.84f ? tie : rest;
    }
}

} // namespace poe
