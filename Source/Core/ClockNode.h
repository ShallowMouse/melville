#pragma once

// MELVILLE's clock outputs: the click, LTC and MIDI clock (24 a beat, with start / stop / continue
// and song position, as drum machines and sequencers have taken since 1983), made from MELVILLE's transport (the tape when a
// reel is loaded, MELVILLE's own clock when not), so they work with or without Hawthorne
// rolling. Its jacks are CLICK and LTC OUT on the MELVILLE panel.

#include "Node.h"
#include "Timecode.h"        // (Hawthorne's: LTC encoding, frame rates)

namespace melville
{

class ClockNode : public Node
{
public:
    /** Where the clock is at the start of this block. */
    struct State
    {
        bool valid = false, rolling = false;
        double pos = 0.0, rate = 0.0;           // clock position and per-sample speed (clock samples)
        double clockRate = 48000.0;             // clock samples per second (the reel's rate, or the interface's)
        double bpm = 0.0, barOne = 0.0;
        int beatsPerBar = 4;
    };
    std::function<State()> getState;            // set before the interface opens (audio thread calls it)

    // settings (any thread)
    std::atomic<bool> clickOn { false }, clickOnlyRecording { false }, ltcOn { true };
    std::atomic<float> clickGain { 0.25f }, accentGain { 1.0f }, ltcGain { 0.3f };
    std::atomic<int> tcRate { 1 };              // hawthorne::timecode::Rate (25)
    std::atomic<double> tcStart { 0.0 };        // timecode at clock zero, seconds
    std::atomic<bool> recording { false };      // (set by whoever knows: for "only while recording")

    /** Where MIDI clock goes (the MIDI patch bay's CLOCK jack; cleared each block by it). */
    juce::MidiBuffer* midiClock = nullptr;

    ClockNode()
    {
        port.id = "clock";
        port.title = "MELVILLE";
        port.sources = { "Click", "LTC" };
    }

    void prepare (double sampleRate, int) override
    {
        sr = sampleRate;
        ltc.prepare (sampleRate);
        clickEnv = 0.0;
        wasRolling = false;
        midiRolling = false;
    }

    /** Timecode frames at a clock position. */
    double timecodeFramesAt (double pos, double clockRate) const noexcept
    {
        const auto r = (hawthorne::timecode::Rate) tcRate.load (std::memory_order_relaxed);
        return (tcStart.load (std::memory_order_relaxed) + pos / juce::jmax (1.0, clockRate)) * hawthorne::timecode::fps (r);
    }

    void pull (float* const* sources, int n) noexcept override
    {
        float* click = sources[0];
        float* lt = sources[1];
        juce::FloatVectorOperations::clear (click, n);
        juce::FloatVectorOperations::clear (lt, n);
        const auto s = getState ? getState() : State();
        if (! s.valid) { wasRolling = false; ltc.reset(); midiClockBlock (s, n); return; }
        midiClockBlock (s, n);

        // --- Click: every beat, the bar's first higher and louder (its pitch follows the tape speed)
        const bool clicking = clickOn.load (std::memory_order_relaxed) && s.bpm > 0.0
                              && (! clickOnlyRecording.load (std::memory_order_relaxed) || recording.load (std::memory_order_relaxed));
        if (clicking)
        {
            const double spb = s.clockRate * 60.0 / s.bpm;
            const int bpb = juce::jmax (1, s.beatsPerBar);
            const float gain = clickGain.load (std::memory_order_relaxed);
            const double speed = s.rolling ? juce::jlimit (0.25, 4.0, std::abs (s.rate) * s.clockRate / sr) : 1.0;
            const double decay = std::exp (-speed / (0.012 * sr));
            for (int k = 0; k < n; ++k)
            {
                if (s.rolling)
                {
                    const double beats = (s.pos + k * s.rate - s.barOne) / spb;
                    const auto b = (juce::int64) std::floor (beats);
                    if (! wasRolling)
                    {
                        lastBeat = (juce::int64) std::ceil (beats - 1.0e-9) - 1;   // a start exactly on a beat clicks
                        wasRolling = true;
                    }
                    if (b != lastBeat)          // a beat crossed (either way: Hawthorne can play backwards)
                    {
                        const auto beat = juce::jmax (b, lastBeat);
                        const bool accent = ((beat % bpb) + bpb) % bpb == 0;
                        freq = accent ? 1760.0 : 1175.0;
                        phase = 0.0;
                        clickEnv = accent ? accentGain.load (std::memory_order_relaxed) : 0.7;
                    }
                    lastBeat = b;
                }
                if (clickEnv > 1.0e-4)
                {
                    click[k] = gain * (float) (clickEnv * std::sin (phase));
                    phase += juce::MathConstants<double>::twoPi * freq * speed / sr;
                    clickEnv *= decay;
                }
            }
        }
        else clickEnv = 0.0;
        if (! s.rolling) wasRolling = false;

        // --- LTC: the clock's timecode while it rolls
        if (ltcOn.load (std::memory_order_relaxed) && s.rolling)
        {
            const auto r = (hawthorne::timecode::Rate) tcRate.load (std::memory_order_relaxed);
            const float g = ltcGain.load (std::memory_order_relaxed);
            for (int k = 0; k < n; ++k)
                lt[k] = ltc.sample (timecodeFramesAt (s.pos + k * s.rate, s.clockRate), r, g);
        }
        else ltc.reset();
    }

private:
    /** MIDI clock: 24 pulses a beat while rolling forwards; song position and continue (start
        at the top) when it rolls, stop when it stops. */
    void midiClockBlock (const State& s, int n) noexcept
    {
        if (midiClock == nullptr) return;
        const bool rolling = s.valid && s.rolling && s.bpm > 0.0 && s.rate > 0.0;
        if (! rolling)
        {
            if (midiRolling) midiClock->addEvent (juce::MidiMessage::midiStop(), 0);
            midiRolling = false;
            return;
        }
        const double spb = s.clockRate * 60.0 / s.bpm;
        const double beats0 = (s.pos - s.barOne) / spb;
        if (! midiRolling)
        {
            const int sixteenths = juce::jmax (0, (int) std::floor (beats0 * 4.0 + 1.0e-6));
            midiClock->addEvent (juce::MidiMessage::songPositionPointer (sixteenths), 0);
            midiClock->addEvent (sixteenths == 0 ? juce::MidiMessage::midiStart() : juce::MidiMessage::midiContinue(), 0);
            midiTick = (juce::int64) std::ceil (beats0 * 24.0 - 1.0e-9) - 1;
            midiRolling = true;
        }
        for (int k = 0; k < n; ++k)
        {
            const auto t = (juce::int64) std::floor ((s.pos + k * s.rate - s.barOne) / spb * 24.0);
            if (t > midiTick + 24 || t < midiTick - 24)
            {
                // a locate while rolling: tell them where we are now
                midiClock->addEvent (juce::MidiMessage::songPositionPointer (juce::jmax (0, (int) (t / 6))), k);
                midiClock->addEvent (juce::MidiMessage::midiContinue(), k);
                midiTick = t;
            }
            else if (t > midiTick) { midiClock->addEvent (juce::MidiMessage::midiClock(), k); midiTick = t; }
        }
    }

    bool midiRolling = false;
    juce::int64 midiTick = 0;
    double sr = 48000.0, phase = 0.0, freq = 1000.0, clickEnv = 0.0;
    juce::int64 lastBeat = 0;
    bool wasRolling = false;
    hawthorne::timecode::LtcEncoder ltc;
};

} // namespace melville
