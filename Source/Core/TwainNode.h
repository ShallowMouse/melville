#pragma once

// TWAIN, MELVILLE's control room: what you listen to and what the players hear.
//  - Monitor: pick a source (the mix, the 2-track, an external input or a headphone feed), its
//    level, dim, mute, mono; out on speaker set A, B or C.
//  - Headphone feeds: four, each its own little mixer of the cue inputs, plus the mix, the
//    click and talkback, with its own level.
//  - Talkback: held, its mic goes into every headphone feed and the speakers dim.

#include "Node.h"
#include <array>

namespace melville
{

class TwainNode : public Node
{
public:
    static constexpr int numCues = 16, numFeeds = 8, numSpeakers = 3;
    enum Source { mix = 0, twoTrack, ext1, ext2, feed1, numSources = feed1 + numFeeds };

    // inputs, in order
    enum In { mixL = 0, mixR, twoTrackL, twoTrackR, ext1L, ext1R, ext2L, ext2R, clickIn, talkIn, cueIn0 };
    static constexpr int numIns = cueIn0 + numCues;

    // settings (any thread)
    std::atomic<int> source { twoTrack }, speakers { 0 };
    std::atomic<float> monitorLevel { 1.0f }, dimLevel { 0.1f }, talkLevel { 1.0f };
    std::atomic<bool> dim { false }, mute { false }, mono { false }, talk { false };
    std::atomic<float> speakerTrim[numSpeakers] { { 1.0f }, { 1.0f }, { 1.0f } };

    struct Feed
    {
        std::atomic<float> cue[numCues];          // each cue input's level (0 = out)
        std::atomic<float> pan[numCues];          // -1 .. +1
        std::atomic<float> mixLevel { 0.0f }, clickLevel { 0.5f }, level { 0.8f };
        Feed() { for (auto& c : cue) c = 0.0f; for (auto& p : pan) p = 0.0f; }
    };
    std::array<Feed, numFeeds> feeds;

    // meters (audio -> UI): peak of the monitor and of each feed
    std::atomic<float> monitorPeak { 0.0f };
    std::array<std::atomic<float>, numFeeds> feedPeak {};

    TwainNode()
    {
        port.id = "twain";
        port.title = "TWAIN";
        port.dests = { "Mix L", "Mix R", "2-track L", "2-track R", "Ext 1 L", "Ext 1 R", "Ext 2 L", "Ext 2 R", "Click", "Talkback" };
        for (int c = 1; c <= numCues; ++c) port.dests.add ("Cue " + juce::String (c));
        for (auto s : { "A", "B", "C" }) { port.sources.add ("Speakers " + juce::String (s) + " L"); port.sources.add ("Speakers " + juce::String (s) + " R"); }
        for (int f = 1; f <= numFeeds; ++f) { port.sources.add ("Phones " + juce::String (f) + " L"); port.sources.add ("Phones " + juce::String (f) + " R"); }
    }

    void prepare (double, int maxBlock) override
    {
        for (auto& b : feedBuf) b.setSize (2, maxBlock);
        smoothedMon = targetMonitorGain();
    }

    void process (const float* const* in, float* const* out, int n) noexcept override
    {
        for (int c = 0; c < port.sources.size(); ++c) juce::FloatVectorOperations::clear (out[c], n);

        // --- the headphone feeds
        const bool talking = talk.load (std::memory_order_relaxed);
        const float tl = talkLevel.load (std::memory_order_relaxed);
        for (int f = 0; f < numFeeds; ++f)
        {
            auto& fd = feeds[(size_t) f];
            auto& b = feedBuf[(size_t) f];
            float* L = b.getWritePointer (0);
            float* R = b.getWritePointer (1);
            juce::FloatVectorOperations::clear (L, n);
            juce::FloatVectorOperations::clear (R, n);
            for (int c = 0; c < numCues; ++c)
            {
                const float g = fd.cue[c].load (std::memory_order_relaxed);
                if (g <= 0.0f) continue;
                const float p = fd.pan[c].load (std::memory_order_relaxed);
                const float gl = g * std::sqrt (0.5f * (1.0f - p)) * 1.41421356f, gr = g * std::sqrt (0.5f * (1.0f + p)) * 1.41421356f;
                juce::FloatVectorOperations::addWithMultiply (L, in[cueIn0 + c], gl, n);
                juce::FloatVectorOperations::addWithMultiply (R, in[cueIn0 + c], gr, n);
            }
            if (const float m = fd.mixLevel.load (std::memory_order_relaxed); m > 0.0f)
            {
                juce::FloatVectorOperations::addWithMultiply (L, in[mixL], m, n);
                juce::FloatVectorOperations::addWithMultiply (R, in[mixR], m, n);
            }
            if (const float k = fd.clickLevel.load (std::memory_order_relaxed); k > 0.0f)
            {
                juce::FloatVectorOperations::addWithMultiply (L, in[clickIn], k, n);
                juce::FloatVectorOperations::addWithMultiply (R, in[clickIn], k, n);
            }
            if (talking)
            {
                juce::FloatVectorOperations::addWithMultiply (L, in[talkIn], tl, n);
                juce::FloatVectorOperations::addWithMultiply (R, in[talkIn], tl, n);
            }
            const float lv = fd.level.load (std::memory_order_relaxed);
            juce::FloatVectorOperations::multiply (L, lv, n);
            juce::FloatVectorOperations::multiply (R, lv, n);
            const int o = 2 * numSpeakers + 2 * f;
            juce::FloatVectorOperations::copy (out[o], L, n);
            juce::FloatVectorOperations::copy (out[o + 1], R, n);
            raise (feedPeak[(size_t) f], juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n)));
        }

        // --- the monitor: the chosen source to the chosen speakers
        const float* srcL = nullptr;
        const float* srcR = nullptr;
        switch (source.load (std::memory_order_relaxed))
        {
            case mix:      srcL = in[mixL];      srcR = in[mixR];      break;
            case twoTrack: srcL = in[twoTrackL]; srcR = in[twoTrackR]; break;
            case ext1:     srcL = in[ext1L];     srcR = in[ext1R];     break;
            case ext2:     srcL = in[ext2L];     srcR = in[ext2R];     break;
            default:
            {
                const int f = juce::jlimit (0, numFeeds - 1, source.load (std::memory_order_relaxed) - feed1);
                srcL = feedBuf[(size_t) f].getReadPointer (0);
                srcR = feedBuf[(size_t) f].getReadPointer (1);
                break;
            }
        }
        const int sp = juce::jlimit (0, numSpeakers - 1, speakers.load (std::memory_order_relaxed));
        float* oL = out[2 * sp];
        float* oR = out[2 * sp + 1];
        const float target = targetMonitorGain() * speakerTrim[sp].load (std::memory_order_relaxed);
        const bool isMono = mono.load (std::memory_order_relaxed);
        const float step = (target - smoothedMon) / (float) juce::jmax (1, n);
        float peak = 0.0f;
        for (int k = 0; k < n; ++k)
        {
            smoothedMon += step;
            float l = srcL[k], r = srcR[k];
            if (isMono) l = r = 0.5f * (l + r);
            oL[k] = l * smoothedMon;
            oR[k] = r * smoothedMon;
            peak = juce::jmax (peak, std::abs (oL[k]), std::abs (oR[k]));
        }
        smoothedMon = target;
        raise (monitorPeak, peak);
    }

private:
    float targetMonitorGain() const noexcept
    {
        if (mute.load (std::memory_order_relaxed)) return 0.0f;
        float g = monitorLevel.load (std::memory_order_relaxed);
        if (dim.load (std::memory_order_relaxed) || talk.load (std::memory_order_relaxed)) g *= dimLevel.load (std::memory_order_relaxed);
        return g;
    }
    static void raise (std::atomic<float>& a, float v) noexcept { if (v > a.load (std::memory_order_relaxed)) a.store (v, std::memory_order_relaxed); }

    std::array<juce::AudioBuffer<float>, numFeeds> feedBuf;
    float smoothedMon = 1.0f;
};

} // namespace melville
