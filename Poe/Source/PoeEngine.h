#pragma once

// POE, MELVILLE's step sequencer, in the manner of the analogue sequencers of the late 60s and
// 70s (the Moog 960, the ARP 1601, Korg's SQ-10): sixteen steps, three rows of knobs (A, B, C)
// and a switch under each step. It runs from MELVILLE's transport and tempo, and plays three
// ways at once: MIDI (into ALCOTT's slots or out to hardware, cabled on the patchbay's MIDI
// tab), and control voltages on its own audio outputs (pitch at 1 V an octave, gate, trigger,
// and rows B and C as 0-5 V) for analogue synths through a DC-coupled interface.
//
//   row A: pitch, quantised to a scale over a range of octaves
//   row B: velocity, or a MIDI CC
//   row C: gate length (how much of the step the note holds), or a MIDI CC
//   switch: PLAY, TIE (hold on from the step before, legato), REST, SKIP (left out of the run)
//
// Eight patterns; the run follows the tape's bar position (steps counted from bar 1), so
// locating and looping land the sequence where it should be. A note into its TRANSPOSE jack
// moves it (middle C = as written).

#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>

namespace poe
{

class Engine : public juce::AudioIODeviceCallback
{
public:
    static constexpr int numSteps = 16, numPatterns = 8;
    enum Mode { play = 0, tie, rest, skip };
    enum Direction { forward = 0, backward, pendulum, randomly };
    enum Outs { pitchCv = 0, gateOut, trigOut, cvB, cvC, numOutputs };
    static constexpr int pitchCvZero = 36;        // the note at 0 V (C2): 1 V an octave up from it

    struct Step
    {
        std::atomic<float> a { 0.0f }, b { 0.75f }, c { 0.5f };
        std::atomic<int> mode { play };
    };
    struct Pattern
    {
        std::array<Step, numSteps> steps;
        std::atomic<int> length { numSteps };
    };

    /** Where the tape is at the block's first sample (MELVILLE's transport). */
    struct Clock
    {
        bool running = false;                 // rolling, with a clock to follow
        double tapePos = 0.0;                 // reel samples
        double tapePerSample = 1.0;
        double barOne = 0.0, reelRate = 48000.0, bpm = 120.0;
    };

    // --- settings (message thread writes, audio thread reads)
    std::atomic<bool> run { false };           // the RUN switch: plays while it's on and the transport rolls
    std::atomic<int> current { 0 };            // the pattern playing (and showing)
    std::atomic<int> rate { 3 };               // index into rateNames(): 1/16
    std::atomic<int> direction { forward };
    std::atomic<float> swing { 0.0f };         // 0 straight .. 0.5 (the off-beat at 75 %)
    std::atomic<int> scale { 1 };              // index into scaleNames(): major
    std::atomic<int> root { 0 };               // 0 = C
    std::atomic<int> octave { 3 };             // knob A at zero: C3 (MIDI 48)
    std::atomic<int> range { 2 };              // octaves knob A covers (1-4)
    std::atomic<float> glideMs { 0.0f };       // the pitch CV's slew
    std::atomic<int> channel { 1 };            // MIDI channel 1-16
    std::atomic<int> bCc { -1 }, cCc { -1 };   // -1: B is velocity / C is gate length; else that CC
    std::atomic<float> fullScale { 10.0f };    // the interface's full-scale output, volts

    // --- for the panel (audio thread writes)
    std::atomic<int> lit { -1 };               // the step playing (-1: stopped)
    std::atomic<int> transpose { 0 };          // semitones from the TRANSPOSE jack
    std::atomic<int> soundingNote { -1 };

    Engine() = default;
    ~Engine() override = default;

    Pattern& pattern (int i) noexcept { return patterns[(size_t) juce::jlimit (0, numPatterns - 1, i)]; }
    const Pattern& pattern (int i) const noexcept { return patterns[(size_t) juce::jlimit (0, numPatterns - 1, i)]; }
    Pattern& now() noexcept { return pattern (current.load (std::memory_order_relaxed)); }

    static juce::StringArray rateNames();
    static juce::StringArray scaleNames();
    static juce::StringArray directionNames();
    static juce::StringArray modeNames();
    static juce::String noteName (int midiNote);
    static double stepsPerBeat (int rateIndex) noexcept;

    /** The MIDI note knob A plays at this setting (before transposing). */
    int noteFor (float a) const noexcept;
    /** The step a running count of steps lands on (-1: none playable), for the direction and skips. */
    int stepAt (juce::int64 count) const noexcept;

    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);
    void clearPattern (int i);
    void copyPattern (int from, int to);
    void randomise (int i, bool pitchOnly);

    /** One block: the CV outs written (any of them may be null), MIDI added to `midiOut`,
        the TRANSPOSE jack's MIDI read from `transposeIn`. Audio thread (or a test). */
    void process (float* const* out, int numOut, int n, const Clock&, const juce::MidiBuffer& transposeIn,
                  juce::MidiBuffer& midiOut) noexcept;
    void prepare (double sr) noexcept { sampleRate = sr; }

    // juce::AudioIODeviceCallback (MELVILLE's POE device)
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    /** Set by the view (message thread, before the device starts): the TRANSPOSE jack. */
    int transposeJack = -1;

private:
    std::array<Pattern, numPatterns> patterns;
    double sampleRate = 48000.0;

    // the run (audio thread)
    bool running = false;
    juce::int64 lastCount = 0;
    int noteOn = -1;                 // the MIDI note held (-1: none)
    int noteChannel = 1;
    double gateEndU = 0.0;           // where (in steps) the held step's gate falls
    bool gateUp = false, holdOn = false;
    int trigLeft = 0, gapLeft = 0;   // samples of trigger pulse left; of the gate's retrigger gap
    double cvPitch = 0.0, cvTarget = 0.0;
    float cvRowB = 0.0f, cvRowC = 0.0f;
    juce::MidiBuffer transposeBlock, midiBlock;

    void release (juce::MidiBuffer& midi, int offset) noexcept;
    void startStep (juce::int64 count, double startU, double lenU, int offset, juce::MidiBuffer& midi) noexcept;
};

} // namespace poe
