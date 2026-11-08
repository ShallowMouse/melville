#pragma once

// MELVILLE's hub: the one program that owns the audio interface. Every part (Hawthorne,
// EMERSON, WHITMAN, THOREAU, PEQUOD Host) runs inside MELVILLE and opens a "MELVILLE"
// audio device (PartDevice) instead of the interface, Hawthorne Route or the EMERSON Link.
// The hub's interface callback runs every part's callback in turn, in one block, with
// the patchbay moving audio between them. Everything runs off the interface's clock.
//
// Order in a block (Hub::buildSteps):
//
//   interface in, network in -> PEQUOD Host -> THOREAU -> ALCOTT -> Hawthorne
//     -> Route bridge (Hawthorne's tracks out)  -> EMERSON -> Route bridge (back into Hawthorne)
//     -> Link bridge (EMERSON to the desk)      -> WHITMAN -> Link bridge (the mix and sends back)
//     -> interface out
//
// A destination fed by a part that runs LATER in the block hears that part's previous
// block (a one-block delay). Patched the default way, that only happens on the way back
// into Hawthorne, which Hawthorne Route already allows for.

#include <juce_audio_devices/juce_audio_devices.h>
#include "ClockNode.h"
#include "TwainNode.h"
#include "Patchbay.h"
#include "Node.h"

namespace melville
{

class PartDevice;
class NetworkNode;
class RouteBridge;
class LinkBridge;

class Hub : private juce::AudioIODeviceCallback,
            private juce::Timer
{
public:
    Hub();
    ~Hub() override;

    /** The one hub of this program. */
    static Hub& get();
    static Hub* getIfExists() noexcept;

    //--------------------------------------------------------------------------
    /** The real audio interface. */
    juce::AudioDeviceManager deviceManager;
    void initialiseAudio (const juce::XmlElement* savedState);

    Patchbay& getPatchbay() noexcept { return patchbay; }
    void defaultPatch();

    /** A part, by id ("hawthorne", "emerson", "whitman", "thoreau", "pequod"). */
    PartNode* findPart (const juce::String& id) const;
    /** Hawthorne Link (the NETWORK panel). */
    NetworkNode& getNetwork() const noexcept { return *network; }
    const std::vector<std::unique_ptr<Node>>& getNodes() const noexcept { return nodes; }

    double getSampleRate() const noexcept { return sampleRate; }
    int getBlockSize() const noexcept     { return blockSize; }
    bool isRunning() const noexcept       { return running.load(); }
    /** The reel's track count (what EMERSON and WHITMAN show). */
    int getVisibleTracks() const noexcept { return visibleTracks; }
    /** Has this part's app opened its device (is it running in the hub)? */
    bool isPartConnected (const juce::String& id) const;

    /** IRVING can run more than once: part ids "pequod", "pequod2" ... and titles "IRVING", "IRVING 2" ... */
    static constexpr int maxIrvings = 4;
    static juce::String irvingId (int n);
    static juce::String irvingTitle (int n);
    /** An app using this part has gone (MELVILLE removed an IRVING): the part is free again. */
    void releasePart (const juce::String& id);

    /** CPU load of the whole block, 0..1 (for the launcher's meter). */
    float getLoad() const noexcept { return load.load (std::memory_order_relaxed); }

    /** Frames run since the interface started, at the start of the block being run now. */
    juce::int64 getBlockFrame() const noexcept { return blockFrame.load (std::memory_order_relaxed); }

    //==========================================================================
    /** Round-trip latency: with an interface output cabled straight to an input, MELVILLE
        sends a few clicks out and times them coming back. What the driver doesn't report
        (converters, cables) becomes the "extra" latency every part is told about, so
        Hawthorne's overdubs land in time. */
    struct LatencyTest
    {
        enum State { idle, running, done, failed };
        std::atomic<int> state { idle };
        std::atomic<int> outChannel { 0 }, inChannel { 0 };
        std::atomic<int> roundTrip { 0 };        // samples, measured
        std::atomic<int> reported { 0 };         // what the driver says (in + out)
        // audio thread
        int ping = 0, waited = 0, gap = 0;
        int results[8] {};
    };
    void startLatencyTest (int outChannel, int inChannel);
    LatencyTest& getLatencyTest() noexcept { return latencyTest; }

    /** Latency the interface adds beyond what its driver reports (samples); reported with the input latency. */
    std::atomic<int> extraLatency { 0 };

    /** Audio thread, at the start of every block (frame, samples, rate): MELVILLE's own clock.
        Set before the interface opens. */
    std::function<void (juce::int64, int, double)> onBlockStart;

    /** MELVILLE's click and LTC out. */
    TwainNode& getTwain() const
    {
        for (auto& n : nodes) if (auto* c = dynamic_cast<TwainNode*> (n.get())) return *c;
        jassertfalse; static TwainNode none; return none;
    }

    ClockNode& getClock() const
    {
        for (auto& n : nodes) if (auto* c = dynamic_cast<ClockNode*> (n.get())) return *c;
        jassertfalse; static ClockNode none; return none;
    }

    /** A PartDevice opening / closing (keeps the parts in step with the interface). */
    void partDeviceOpened (PartDevice&);
    void partDeviceClosed (PartDevice&);

    /** For tests: run one block as if the interface had called. */
    void processBlockForTest (const float* const* in, int numIn, float* const* out, int numOut, int n)
    {
        audioDeviceIOCallbackWithContext (in, numIn, out, numOut, n, {});
    }
    void prepareForTest (double rate, int block) { prepare (rate, block, 2, 2); }

private:
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int n, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void timerCallback() override;

    void addNode (std::unique_ptr<Node>);
    void updateVisibleTracks (int numTracks);
    void reopenPart (PartNode&);
    void prepare (double rate, int block, int numIn, int numOut);
    void gather (const Matrix&, int firstDest, int numDests, float* const* into, int n) noexcept;

    Patchbay patchbay;
    std::vector<std::unique_ptr<Node>> nodes;
    std::vector<Step> steps;
    Node* interfaceNode = nullptr;
    NetworkNode* network = nullptr;

    // one buffer per source channel: what each part / bridge put out this (or last) block
    juce::AudioBuffer<float> sourcePool;
    // scratch for gathering a node's destinations
    juce::AudioBuffer<float> destScratch;
    std::vector<float*> destPtrs;
    std::vector<float*> sourcePtrs;

    int visibleTracks = 0;
    double sampleRate = 48000.0;
    int blockSize = 512;
    std::atomic<bool> running { false };
    std::atomic<float> load { 0.0f };
    std::atomic<juce::int64> blockFrame { 0 };
    LatencyTest latencyTest;
    void runLatencyTest (const float* const* in, int numIn, float* const* out, int numOut, int n);
};

} // namespace melville
