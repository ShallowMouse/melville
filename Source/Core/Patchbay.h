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
// NORMALS: like a hardware TT bay, the usual wiring is behind the bay. A destination can be
// normalled from a source; with no cable in either jack the normal carries the signal.
// Plugging a cable into the destination breaks it. A HALF normal (the usual kind) keeps
// working when a cable is plugged into the source (a mult out); a FULL normal breaks then too.
// PULL ALL pulls the cables; the normals stay, so the studio goes back to its usual wiring.
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
    void clearAll();
    /** Connect each channel of port A's sources to the channel of B's dests with the same name. */
    int connectByName (const juce::String& fromPort, const juce::String& toPort);
    /** Connect A's sources 1..n to B's dests 1..n in order (from the given offsets). */
    int connectInOrder (const juce::String& fromPort, int fromOffset, const juce::String& toPort, int toOffset, int n);

    /** Every cable, for the UI (not the normals). */
    std::vector<std::pair<int, Cord>> getAllCords() const;   // (dest, cord)
    /** What's actually connected: the cables and the normals no cable has broken. */
    std::vector<std::pair<int, Cord>> getLiveCords() const;
    bool isLive (int source, int dest) const;

    //--------------------------------------------------------------------------
    // Normals (message thread)
    /** The source a destination is normalled from (-1: none). */
    int normalOf (int dest) const;
    bool isFullNormal (int dest) const;
    /** Normalled and no cable has broken it. */
    bool isNormalLive (int dest) const;
    bool isSourcePlugged (int source) const;     // a cable's in this output
    bool isDestPlugged (int dest) const;         // a cable's in this input
    /** Normal a destination from a source (-1 takes the normal out). */
    void setNormal (int source, int dest, bool full = false);
    void clearNormals();
    /** Each normal: (dest, source). */
    std::vector<std::pair<int, int>> getAllNormals() const;

    /** While one of these is alive, connect / connectByName / connectInOrder wire normals, not cables. */
    struct NormalWiring
    {
        explicit NormalWiring (Patchbay& p) : pb (p) { pb.wiringNormals = true; }
        ~NormalWiring() { pb.wiringNormals = false; pb.publish(); }
        Patchbay& pb;
    };

    // Saving: cords by port id + channel name, so a patch survives ports growing.
    std::unique_ptr<juce::XmlElement> toXml() const;
    void fromXml (const juce::XmlElement&);
private:
    void fromXmlCurrent (const juce::XmlElement&);
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
    /** A patch from before normals was loaded: wire the usual normals (the hub's default patch). */
    std::function<void()> onDefaultNormals;
    /** A patch from before MIDI was on the patchbay was loaded (the hub adds MIDI's usual cords). */
    std::function<void()> onOlderPatch;

private:
    void publish();
    void plug (int source, int dest, float gain);   // a cable, or a normal while wiring
    Matrix live_() const;

    struct Normal { int source = -1; bool full = false; };
    std::vector<Normal> normals;                    // [dest]
    bool wiringNormals = false;

    std::vector<Port> ports;
    int numSources = 0, numDests = 0;
    Matrix edit;                                   // the message thread's copy
    std::atomic<Matrix*> live { nullptr };
    struct Retired { Matrix* m; juce::uint64 afterBlock; };
    std::vector<Retired> retired;
    std::atomic<juce::uint64> blocksDone { 0 };
};

} // namespace melville
