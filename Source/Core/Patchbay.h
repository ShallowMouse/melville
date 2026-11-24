#pragma once

// MELVILLE's patchbay: every part's outputs are SOURCES, every part's inputs are
// DESTINATIONS. A destination hears the sum of the sources patched to it (each with
// its own gain). Many destinations can take the same source (a mult).
//
// Sources and destinations are flat channel numbers, grouped into ports (one port per
// part, bridge or the interface) so the UI can show them by part. Channels carry
// names; the default patch connects channels whose names match ("Hawthorne track 7"
// out of the Route bridge -> "Hawthorne track 7" into EMERSON).
//
// Cables on the front, normals behind. A cable is the session's (saved with the reel). A
// normal is wired behind the bay, from an output to an input, and carries until a cable is
// plugged into that input (a half-normal: a cable out of the output takes a mult and leaves the
// normal alone). Normals belong to the studio, not the session: they're saved once for every
// session. The audio jacks and the MIDI ones are kept apart: saving, opening, DEFAULT, PULL ALL
// and SAVE AS USUAL each work on one side (Scope). (Sessions from before, saved with their
// normals, load those as cables, so they sound as they did.)
//
// The audio thread reads a Matrix through an atomic pointer; the message thread
// builds a new one and swaps it in. Old ones are freed later on the message thread
// (Patchbay::collectGarbage), never on the audio thread.

#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace melville
{

struct Port
{
    juce::String id;             // "hawthorne", "route", "emerson", ... (stable, saved in patches)
    juce::String title;          // "HAWTHORNE", shown in the UI
    juce::StringArray sources;   // its output channels' names
    juce::StringArray dests;     // its input channels' names
    int firstSource = 0, firstDest = 0;   // flat numbers (set by Patchbay::addPort)
};

struct Cord
{
    int source = 0;
    float gain = 1.0f;
};

/** What the audio thread reads: for every destination, the cords into it. */
struct Matrix
{
    std::vector<std::vector<Cord>> into;   // [dest] -> cords
};

class Patchbay
{
public:
    /** Which jacks something works on: all of them, the audio ones or the MIDI ones. */
    enum class Scope { all, audio, midi };

    Patchbay() = default;
    ~Patchbay();

    //--------------------------------------------------------------------------
    // Setting up (message thread, before audio starts)
    int addPort (Port p);
    const std::vector<Port>& getPorts() const noexcept { return ports; }
    const Port* findPort (const juce::String& id) const;
    int getNumSources() const noexcept { return numSources; }
    int getNumDests() const noexcept   { return numDests; }

    juce::String sourceName (int s) const;   // "EMERSON / To Hawthorne track 3"
    juce::String destName (int d) const;
    int findSource (const juce::String& portId, const juce::String& channel) const;
    int findDest (const juce::String& portId, const juce::String& channel) const;

    //--------------------------------------------------------------------------
    // Patching (message thread). Each call publishes a new matrix to the audio thread.
    /** The MIDI patch bay's jacks (MIDI goes only to MIDI). */
    bool isMidiSource (int source) const;
    bool isMidiDest (int dest) const;
    bool isConnected (int source, int dest) const;
    void connect (int source, int dest, float gain = 1.0f);
    void disconnect (int source, int dest);
    void clearDest (int dest);
    void clearAll (Scope = Scope::all);
    /** Whether a destination is one of a scope's jacks. */
    bool inScope (int dest, Scope sc) const { return sc == Scope::all || isMidiDest (dest) == (sc == Scope::midi); }
    /** Connect each channel of port A's sources to the channel of B's dests with the same name. */
    int connectByName (const juce::String& fromPort, const juce::String& toPort);
    /** Connect A's sources 1..n to B's dests 1..n in order (from the given offsets). */
    int connectInOrder (const juce::String& fromPort, int fromOffset, const juce::String& toPort, int toOffset, int n);

    /** Every cable. */
    std::vector<std::pair<int, Cord>> getAllCords() const;   // (dest, cord)
    /** What's connected now: the cables, and the normals no cable has broken. */
    std::vector<std::pair<int, Cord>> getLiveCords() const;

    //--------------------------------------------------------------------------
    // Normals, behind the bay (one into each input at most)
    void setNormal (int source, int dest);       // (replaces the one into that input)
    void removeNormal (int dest);
    void clearNormals (Scope = Scope::all);
    int normalInto (int dest) const;              // its source, or -1
    bool isNormalCarrying (int dest) const { return normalInto (dest) >= 0 && ! isDestPlugged (dest); }
    const std::vector<std::pair<int, int>>& getNormals() const noexcept { return normals; }   // (source, dest)
    std::unique_ptr<juce::XmlElement> normalsToXml() const;
    void normalsFromXml (const juce::XmlElement&);
    /** The normals changed (the hub saves them). */
    std::function<void()> onNormalsChanged;
    bool isSourcePlugged (int source) const;     // a cable's in this output
    bool isDestPlugged (int dest) const;         // a cable's in this input

    /** Your usual wiring (the cables DEFAULT puts back), and putting it back. Older files of
        normals load as cables. */
    std::unique_ptr<juce::XmlElement> wiringToXml() const;
    void wiringFromXml (const juce::XmlElement&);

    // Saving: cords by port id + channel name, so a patch survives ports growing. With a scope,
    // only that side's cables are saved, or replaced (the other side's are left as they are).
    std::unique_ptr<juce::XmlElement> toXml (Scope = Scope::all) const;
    void fromXml (const juce::XmlElement&, Scope = Scope::all);
    /** A saved cord's side (by its ports: MIDI's are on the "midi" port). */
    static bool isMidiCord (const juce::XmlElement& cord) { return cord.getStringAttribute ("toPort") == "midi"; }
private:
    void fromXmlCurrent (const juce::XmlElement&, Scope);
public:

    //--------------------------------------------------------------------------
    /** Audio thread: the current matrix (never null once a port exists). */
    const Matrix* getMatrix() const noexcept { return live.load (std::memory_order_acquire); }

    /** Audio thread: call once per block, so the message thread knows when an old matrix is free. */
    void audioBlockDone() noexcept { blocksDone.fetch_add (1, std::memory_order_release); }

    /** Message thread (a timer): free matrices the audio thread can no longer be reading. */
    void collectGarbage (bool audioRunning);

    /** Changes (for the UI). */
    std::function<void()> onChange;
    /** A patch from before MIDI was on the patchbay was loaded (the hub adds MIDI's usual cords). */
    std::function<void()> onOlderPatch;

private:
    void publish();
    void plug (int source, int dest, float gain);
    /** Cables and (from older files) normals, all as cables: a normal becomes a cable where
        nothing was plugged into its input (and, for a full normal, its output). */
    void loadCordsAndNormals (const juce::XmlElement&, Scope = Scope::all);
    std::pair<juce::String, juce::String> where (int flat, bool isSource) const;
    std::vector<std::pair<int, int>> normals;      // (source, dest) behind the bay
    void normalsChanged() { publish(); if (onNormalsChanged) onNormalsChanged(); }

    std::vector<Port> ports;
    int numSources = 0, numDests = 0;
    Matrix edit;                                   // the message thread's copy
    std::atomic<Matrix*> live { nullptr };
    struct Retired { Matrix* m; juce::uint64 afterBlock; };
    std::vector<Retired> retired;
    std::atomic<juce::uint64> blocksDone { 0 };
};

} // namespace melville
