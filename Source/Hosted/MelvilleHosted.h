#pragma once

// What a part (Hawthorne, EMERSON, WHITMAN, THOREAU, PEQUOD Host) calls when it's built
// into MELVILLE. Each app has its own Source/Melville.h that includes this when
// MELVILLE_HOSTED is set, and does nothing when the app is built on its own:
//
//     if (! melville::attach (deviceManager, "emerson"))
//         ... the app's own device setup, as before ...
//
// attach() gives the app's AudioDeviceManager only the "MELVILLE" device type and opens
// the part's device on it: the part then runs inside MELVILLE's hub, at the interface's
// rate and block, with the patchbay on its inputs and outputs.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <map>

namespace melville
{
    /** Is this program MELVILLE (rather than the app on its own)? */
    bool isHosted() noexcept;

    /** Open the part's MELVILLE device on this manager. False if there's no hub (then
        set the device up as the app would on its own). Message thread. */
    bool attach (juce::AudioDeviceManager&, const char* partId);

    /** Inside MELVILLE a part has no audio settings (MELVILLE's INTERFACE picks the device):
        where it had a device selector for its MIDI inputs, it shows this instead. */
    std::unique_ptr<juce::Component> midiInputsPanel (juce::AudioDeviceManager&);

    /** MELVILLE: the next app to attach takes this part instead of the one it asks for (a
        second IRVING asks for "pequod" and gets "pequod2"). Empty: as asked. */
    void setNextPart (const juce::String& partId);

    /** The part's manager is going away (call before it's destroyed). */
    void detach (juce::AudioDeviceManager&);

    //==========================================================================
    /** MELVILLE's transport and tempo. MELVILLE owns the tempo, meter and bar 1. With a reel on
        Hawthorne's deck the tape is the clock (Hawthorne publishes where it is every block, and
        the tempo lives on the reel too); with no reel MELVILLE runs a clock of its own, so THOREAU
        and IRVING still play. Everything follows it directly (no network). Use the functions
        below (play(), setTempo() ...) rather than the hooks. */
    struct Transport
    {
        // --- Hawthorne's hooks (message thread): its tape machine
        std::function<void()> play, stop, record, returnToZero;
        std::function<void (int direction)> wind;           // -1 rewind, +1 fast forward, 0 let go
        std::function<void (double bpm, int beatsPerBar, double barOne)> writeTempo;   // onto the reel (barOne: tape samples)

        // --- State (any thread)
        std::atomic<bool> available { false };              // Hawthorne is there
        std::atomic<bool> rolling { false }, recording { false }, reelLoaded { false };
        std::atomic<juce::int64> position { 0 };             // tape position (reel samples), for the counter
        std::atomic<double> reelRate { 48000.0 };            // the reel's sample rate
        std::atomic<double> bpm { 120.0 };                   // MELVILLE's tempo (always set)
        std::atomic<int> beatsPerBar { 4 };
        std::atomic<double> barOne { 0.0 };                  // tape position of bar 1's downbeat
        std::atomic<int> tempoRev { 0 };                     // changes when the tempo or bar 1 changes

        // --- Audio thread: Hawthorne publishes, once per block, where an input sample at the
        //     start of its block lands on tape and how fast the tape goes (0 when stopped).
        std::atomic<double> tapePos { 0.0 }, tapePerSample { 0.0 };
        std::atomic<juce::int64> publishedFrame { -1 };      // the hub frame of that block's start
        std::atomic<int> flags { 0 };                        // 1 rolling, 2 recording, 4 reel loaded
    };
    Transport& transport() noexcept;

    // MELVILLE's transport keys (message thread): the tape's with a reel on the deck,
    // MELVILLE's own clock without one
    void play();
    void stop();
    void record();
    void returnToZero();
    void wind (int direction);
    bool isRolling() noexcept;
    /** MELVILLE's tempo, meter and bar 1 (written onto the reel if there is one). */
    void setTempo (double bpm, int beatsPerBar, double barOne);
    /** A reel came onto the deck with this tempo (bpm 0: none): MELVILLE takes it, or puts its own on it. */
    void reelTempo (double bpm, int beatsPerBar, double barOne);

    /** Hub, audio thread, at the start of every block: MELVILLE's own clock (no reel). */
    void clockBlock (juce::int64 frame, int numSamples, double sampleRate) noexcept;

    /** Audio thread: MELVILLE's frame count at the start of the block being run now (every
        part sees the same number in the same block). */
    juce::int64 currentFrame() noexcept;

    /** Where frame f lands on tape, from the latest published block; false if Hawthorne isn't
        publishing (no tape clock). */
    bool tapePositionAt (juce::int64 frame, double& tapePos, double& tapePerSample, int& flags) noexcept;

    //==========================================================================
    /** MELVILLE's session file: one file for the whole studio. Each part (after attach) says
        how to put its state in it and take it back. Message thread. */
    struct SessionPart
    {
        std::function<std::unique_ptr<juce::XmlElement>()> save;     // the part's state, any tag
        std::function<void (const juce::XmlElement&)> load;          // what save() gave
    };
    void addToSession (juce::AudioDeviceManager&, SessionPart);

    /** MELVILLE: the parts in the session, by part id ("hawthorne", "pequod2" ...). */
    const std::map<juce::String, SessionPart>& sessionParts();
}
