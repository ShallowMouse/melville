#pragma once

// ALCOTT: MELVILLE's instrument rack. Eight slots, each a PEQUOD instrument with a chain of
// PEQUOD effects after it, played by whatever is cabled into its MIDI IN on MELVILLE's patchbay
// (keyboards, THOREAU's steps, the clock). Every slot
// has its own stereo output on the patchbay, and there's a mix of them all.

#include <JuceHeader.h>
#include "PequodNative.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace alcott
{

class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback
{
public:
    static constexpr int numSlots = 8;
    /** Outputs: slot 1 L / R ... slot 8 L / R, then the mix L / R. */
    static constexpr int numOutputs = 2 * numSlots + 2;

    /** One PEQUOD plugin in a slot (the instrument, or an effect after it). */
    struct Unit
    {
        juce::String id;                                     // "pequod:<class id>"
        juce::String name;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::atomic<bool> bypassed { false };
        int uid = 0;
    };

    struct Slot
    {
        juce::String name;                                   // (empty: the instrument's name)
        std::unique_ptr<Unit> instrument;
        std::vector<std::unique_ptr<Unit>> effects;
        std::atomic<int> channel { 0 };                      // only notes on this MIDI channel (0: any)
        std::atomic<float> levelDb { 0.0f };
        std::atomic<bool> mute { false };
        std::atomic<float> peak { 0.0f };                    // (audio -> UI)
        std::atomic<float> noteLamp { 0.0f };                // a note came in (audio -> UI)

        juce::String shownName() const { return name.isNotEmpty() ? name : instrument != nullptr ? instrument->name : juce::String(); }
    };

    Engine();
    ~Engine() override;

    Slot& slot (int i) { return slots[(size_t) juce::jlimit (0, numSlots - 1, i)]; }
    int jack (int i) const noexcept { return jacks[(size_t) juce::jlimit (0, numSlots - 1, i)]; }

    /** The PEQUOD instruments and effects. */
    static std::vector<const PequodNative::Info*> instruments();
    static std::vector<const PequodNative::Info*> effects();

    // Message thread: changing a slot's chain (made outside the audio lock, swapped in under it)
    bool setInstrument (int slot, const juce::String& id);         // empty: none
    bool addEffect (int slot, const juce::String& id, int at = -1);
    void removeEffect (int slot, int uid);
    void moveEffect (int slot, int uid, int delta);
    void clearSlot (int slot);

    /** Every slot, as XML (and back): instruments, effects and their settings. */
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);

    /** Notes played from the window's keys (any thread): straight into one slot. */
    void playNote (int slot, int note, float velocity);

    std::function<void()> onChanged;                     // message thread: anything to save

    /** How long THOREAU's notes hold (it sends no note-offs): a 1/n note, 4 8 16 or 32. */
    std::atomic<int> thoreauLength { 16 };

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    // MidiInputCallback (the keyboards)
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    double getSampleRate() const noexcept { return sampleRate; }
    /** For checks: as if a device of this rate and block had started, and one block of it. */
    void prepareForTest (double rate, int block);
    void processForTest (float* const* out, int numOut, int n) { audioDeviceIOCallbackWithContext (nullptr, 0, out, numOut, n, {}); }

private:
    class PlayHead;
    std::unique_ptr<Unit> makeUnit (const juce::String& id, juce::String& error);
    void retire (std::unique_ptr<Unit>);
    void process (float* const* out, int numOut, int n) noexcept;

    std::array<Slot, numSlots> slots;
    juce::CriticalSection chainLock;                 // the audio thread takes it with a try-lock
    juce::MidiMessageCollector keys;                 // MIDI keyboards
    struct Direct { int slot, note; float velocity; };
    juce::AbstractFifo directFifo { 256 };
    std::array<Direct, 256> direct {};
    juce::SpinLock directWrite;

    double sampleRate = 48000.0;
    int blockSize = 512;
    int nextUid = 1;

    // audio thread
    juce::AudioBuffer<float> work, mix;
    juce::MidiBuffer keyBlock;
    std::array<juce::MidiBuffer, numSlots> slotMidi;
    std::array<int, numSlots> jacks {};              // each slot's MIDI IN on the patchbay
    juce::MidiBuffer cabled;
    std::unique_ptr<PlayHead> playHead;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace alcott
