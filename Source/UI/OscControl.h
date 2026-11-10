#pragma once

// MELVILLE's control surface over OSC: TWAIN's monitor keys and level, and the transport, from
// a tablet (TouchOSC) or, later, the hardware TWAIN. Listens on a UDP port (8100) and answers
// whoever sent the last message (or a fixed address) on the reply port (9100), with every lamp and
// the display, so a surface's buttons light like the hardware's LEDs. Set up in MELVILLE's
// INTERFACE window. (IRVING's own OSC is on 9000 / 9001.)
//
//   /twain/source/mix  /twain/source/2track  /twain/source/ext1  /twain/source/ext2     press
//   /twain/speakers/a  /twain/speakers/b  /twain/speakers/c                            press
//   /twain/dim  /twain/mute  /twain/mono                                     press: on / off
//   /twain/ref                                                 press: the level to 0 dB
//   /twain/level f       0..1 = -60 .. +6 dB (a knob)
//   /twain/level/step f  half-dB steps, + or - (what an encoder sends)
//   /transport/rtz  /transport/stop  /transport/play  /transport/rec                    press
//   /transport/rew  /transport/ffw                      held: winds while it's held down
//   /melville/sync                                      any surface: send me everything
//
// Back to the surface: each key's address with 1 (lit) or 0; /twain/level f; /twain/level/text s;
// /twain/display/1 s, /twain/display/2 s; /transport/counter s.

#include <JuceHeader.h>
#include "../Core/TwainNode.h"
#include "../Hosted/MelvilleHosted.h"
#include <map>

namespace melville
{

class OscControl : private juce::Thread, private juce::Timer
{
public:
    struct Settings
    {
        bool enabled = true;
        int listenPort = 8100, replyPort = 9100;
        juce::String replyHost;                 // empty: answer whoever's sending

        std::unique_ptr<juce::XmlElement> toXml() const
        {
            auto x = std::make_unique<juce::XmlElement> ("OSC");
            x->setAttribute ("enabled", enabled);
            x->setAttribute ("listen", listenPort);
            x->setAttribute ("reply", replyPort);
            x->setAttribute ("replyHost", replyHost);
            return x;
        }
        static Settings fromXml (const juce::XmlElement* x)
        {
            Settings s;
            if (x == nullptr) return s;
            s.enabled = x->getBoolAttribute ("enabled", true);
            s.listenPort = juce::jlimit (1, 65535, x->getIntAttribute ("listen", 8100));
            s.replyPort = juce::jlimit (1, 65535, x->getIntAttribute ("reply", 9100));
            s.replyHost = x->getStringAttribute ("replyHost").trim();
            return s;
        }
    };

    /** The one MELVILLE runs (for its settings), or nullptr. */
    static OscControl*& instance() { static OscControl* o = nullptr; return o; }
    /** The settings changed (to save them). */
    std::function<void (const Settings&)> onSettingsChanged;

    explicit OscControl (TwainNode& t) : juce::Thread ("MELVILLE OSC"), twain (t) { instance() = this; }
    ~OscControl() override { stop(); if (instance() == this) instance() = nullptr; }

    /** Use these settings: stops, and starts again if it's on. Returns false if the port couldn't be opened. */
    bool apply (const Settings& s)
    {
        stop();
        settings = s;
        surfaceHost = {};
        connected = false;
        lastHeardMs = 0;
        if (onSettingsChanged) onSettingsChanged (settings);
        if (! settings.enabled) return true;
        socket = std::make_unique<juce::DatagramSocket> (false);
        if (! socket->bindToPort (settings.listenPort)) { socket.reset(); failed = true; return false; }
        failed = false;
        port = settings.listenPort;
        if (settings.replyHost.isNotEmpty()) connectTo (settings.replyHost);
        startThread();
        startTimerHz (15);
        return true;
    }

    void stop()
    {
        stopTimer();
        if (socket != nullptr) socket->shutdown();
        stopThread (1000);
        socket.reset();
        port = 0;
        failed = false;
    }

    const Settings& getSettings() const noexcept { return settings; }
    int getPort() const noexcept { return port; }
    juce::String getAddress() const
    {
        return port == 0 ? juce::String() : juce::IPAddress::getLocalAddress().toString() + ":" + juce::String (port);
    }
    juce::String getSurface() const { return lastHeardMs > 0 ? lastFrom : juce::String(); }   // who's controlling (empty: nobody yet)

    /** One line for the settings: off, listening where, who's on it. */
    juce::String getStatus() const
    {
        if (! settings.enabled) return "Off.";
        if (failed || port == 0) return "Port " + juce::String (settings.listenPort) + " is taken (by another program?): pick another.";
        juce::String s = "Listening on " + getAddress() + ". ";
        if (lastHeardMs == 0) s << "Nothing heard yet.";
        else
        {
            const auto ago = (juce::Time::getMillisecondCounter() - lastHeardMs) / 1000u;
            s << "Last heard from " << lastFrom << (ago < 2 ? juce::String (" just now") : " " + juce::String ((int) ago) + " s ago") << ".";
        }
        if (connected) s << " Answering " << surfaceHost << ":" << settings.replyPort << ".";
        return s;
    }

private:
    //==========================================================================
    // receiving (its own thread; the work is done on the message thread)
    void run() override
    {
        juce::HeapBlock<char> buf (65536);
        while (! threadShouldExit())
        {
            if (socket->waitUntilReady (true, 200) <= 0) continue;
            juce::String from;
            int fromPort = 0;
            const int n = socket->read (buf.get(), 65536, false, from, fromPort);
            if (n <= 0) continue;
            std::vector<Msg> msgs;
            parse (buf.get(), buf.get() + n, msgs, 0);
            if (msgs.empty()) continue;
            juce::MessageManager::callAsync ([safe = juce::WeakReference<OscControl> (this), msgs, from]
            {
                if (auto* self = safe.get())
                {
                    self->lastFrom = from;
                    self->lastHeardMs = juce::jmax (1u, juce::Time::getMillisecondCounter());
                    if (self->settings.replyHost.isEmpty() && from != self->surfaceHost) self->connectTo (from);
                    for (auto& m : msgs) self->handle (m);
                    self->resendAll = true;          // (and put every lamp right, the pressed one too)
                }
            });
        }
    }

    struct Msg { juce::String address; float value = 1.0f; bool hasValue = false; };

    static juce::uint32 be32 (const char* p) noexcept
    {
        auto u = (const juce::uint8*) p;
        return ((juce::uint32) u[0] << 24) | ((juce::uint32) u[1] << 16) | ((juce::uint32) u[2] << 8) | (juce::uint32) u[3];
    }

    static bool readString (const char*& p, const char* end, juce::String& out)
    {
        const char* start = p;
        while (p < end && *p != 0) ++p;
        if (p >= end) return false;
        out = juce::String::fromUTF8 (start, (int) (p - start));
        p = start + (((size_t) (p - start) + 4) & ~(size_t) 3);
        return p <= end;
    }

    /** An OSC packet: a message, or a bundle of them. */
    static void parse (const char* p, const char* end, std::vector<Msg>& out, int depth)
    {
        if (end - p < 4 || depth > 4) return;
        if (end - p >= 16 && std::memcmp (p, "#bundle", 8) == 0)
        {
            p += 16;                                   // ("#bundle" and the time tag)
            while (end - p >= 4)
            {
                const auto size = (int) be32 (p);
                p += 4;
                if (size < 0 || size > end - p) return;
                parse (p, p + size, out, depth + 1);
                p += size;
            }
            return;
        }
        Msg m;
        if (! readString (p, end, m.address) || ! m.address.startsWithChar ('/')) return;
        juce::String tags;
        if (p < end && *p == ',' && readString (p, end, tags))
        {
            for (int k = 1; k < tags.length(); ++k)
            {
                const auto t = tags[k];
                float v = 0.0f;
                bool num = true;
                if (t == 'f' || t == 'i')
                {
                    if (end - p < 4) break;
                    const auto u = be32 (p); p += 4;
                    if (t == 'i') v = (float) (juce::int32) u;
                    else { float f; std::memcpy (&f, &u, 4); v = f; }
                }
                else if (t == 'd' || t == 'h')
                {
                    if (end - p < 8) break;
                    const auto u = ((juce::uint64) be32 (p) << 32) | be32 (p + 4); p += 8;
                    if (t == 'h') v = (float) (juce::int64) u;
                    else { double d; std::memcpy (&d, &u, 8); v = (float) d; }
                }
                else if (t == 'T') v = 1.0f;
                else if (t == 'F') v = 0.0f;
                else if (t == 's') { juce::String s; if (! readString (p, end, s)) break; num = false; }
                else break;
                if (num && ! m.hasValue) { m.value = v; m.hasValue = true; }
            }
        }
        out.push_back (m);
    }

    //==========================================================================
    // doing it (message thread)
    static float levelDb (float gain) { return gain <= 0.0f ? -60.0f : juce::jlimit (-60.0f, 6.0f, juce::Decibels::gainToDecibels (gain)); }
    static float dbGain (float db)    { return db <= -59.9f ? 0.0f : juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 6.0f, db)); }

    void handle (const Msg& m)
    {
        const auto& a = m.address;
        const bool press = ! m.hasValue || m.value > 0.5f;
        auto onPress = [press] (auto f) { if (press) f(); };

        static const std::map<juce::String, int> sources { { "/twain/source/mix", TwainNode::mix }, { "/twain/source/2track", TwainNode::twoTrack },
                                                           { "/twain/source/ext1", TwainNode::ext1 }, { "/twain/source/ext2", TwainNode::ext2 } };
        if (auto it = sources.find (a); it != sources.end()) { onPress ([&] { twain.source = it->second; }); return; }
        if (a.startsWith ("/twain/speakers/") && a.length() == 17)
        {
            const int k = juce::CharacterFunctions::toLowerCase (a.getLastCharacter()) - 'a';
            if (juce::isPositiveAndBelow (k, TwainNode::numSpeakers)) onPress ([&] { twain.speakers = k; });
            return;
        }
        if (a == "/twain/dim")  { onPress ([&] { twain.dim  = ! twain.dim.load(); });  return; }
        if (a == "/twain/mute") { onPress ([&] { twain.mute = ! twain.mute.load(); }); return; }
        if (a == "/twain/mono") { onPress ([&] { twain.mono = ! twain.mono.load(); }); return; }
        if (a == "/twain/ref")  { onPress ([&] { twain.monitorLevel = 1.0f; });         return; }
        if (a == "/twain/level" && m.hasValue)
        {
            twain.monitorLevel = dbGain (-60.0f + 66.0f * juce::jlimit (0.0f, 1.0f, m.value));
            return;
        }
        if (a == "/twain/level/step" && m.hasValue && m.value != 0.0f)
        {
            const float db = levelDb (twain.monitorLevel.load());
            twain.monitorLevel = dbGain (db + 0.5f * m.value);
            return;
        }
        if (a == "/transport/play") { onPress ([] { melville::play(); });         return; }
        if (a == "/transport/stop") { onPress ([] { melville::stop(); });         return; }
        if (a == "/transport/rec")  { onPress ([] { melville::record(); });       return; }
        if (a == "/transport/rtz")  { onPress ([] { melville::returnToZero(); }); return; }
        if (a == "/transport/rew" || a == "/transport/ffw")
        {
            melville::wind (press ? (a.endsWith ("rew") ? -1 : 1) : 0);
            return;
        }
        // (/melville/sync and anything else: just the lamps, below)
    }

    //==========================================================================
    // answering (message thread)
    void connectTo (const juce::String& host)
    {
        surfaceHost = host;
        sender.disconnect();
        connected = sender.connect (host, settings.replyPort);
        sent.clear();
    }

    void timerCallback() override
    {
        if (! connected) return;
        if (++ticks % 30 == 0) resendAll = true;       // (every two seconds: a surface that's just woken up)

        const int src = twain.source.load(), spk = twain.speakers.load();
        lamp ("/twain/source/mix", src == TwainNode::mix);
        lamp ("/twain/source/2track", src == TwainNode::twoTrack);
        lamp ("/twain/source/ext1", src == TwainNode::ext1);
        lamp ("/twain/source/ext2", src == TwainNode::ext2);
        for (int k = 0; k < TwainNode::numSpeakers; ++k)
            lamp ("/twain/speakers/" + juce::String::charToString ((juce::juce_wchar) ('a' + k)), spk == k);
        lamp ("/twain/dim", twain.dim.load());
        lamp ("/twain/mute", twain.mute.load());
        lamp ("/twain/mono", twain.mono.load());
        lamp ("/twain/ref", std::abs (levelDb (twain.monitorLevel.load())) < 0.25f);

        const float db = levelDb (twain.monitorLevel.load());
        value ("/twain/level", (db + 60.0f) / 66.0f);
        const juce::String levelText = twain.mute.load() ? juce::String ("MUTE")
                                     : db <= -59.9f ? juce::String ("-INF")
                                     : juce::String (db, 1) + " dB";
        text ("/twain/level/text", levelText);

        static const char* names[] = { "MIX", "2-TRACK", "EXT 1", "EXT 2" };
        const juce::String listening = juce::isPositiveAndBelow (src, 4) ? juce::String (names[src]) : "PHONES " + juce::String (src - TwainNode::feed1 + 1);
        text ("/twain/display/1", "LISTENING  " + listening + "   SPEAKERS " + juce::String::charToString ((juce::juce_wchar) ('A' + spk)));
        juce::StringArray flags;
        if (twain.dim.load())  flags.add ("DIM");
        if (twain.mute.load()) flags.add ("MUTE");
        if (twain.mono.load()) flags.add ("MONO");
        text ("/twain/display/2", flags.isEmpty() ? juce::String ("MELVILLE  TWAIN") : flags.joinIntoString ("  "));

        auto& t = melville::transport();
        const bool rolling = melville::isRolling(), recording = t.recording.load();
        lamp ("/transport/play", rolling);
        lamp ("/transport/rec", recording);
        lamp ("/transport/stop", ! rolling);
        const double secs = (double) t.position.load() / juce::jmax (1.0, t.reelRate.load());
        const int whole = (int) std::floor (juce::jmax (0.0, secs));
        text ("/transport/counter", juce::String::formatted ("%02d:%02d:%02d.%d", whole / 3600, (whole / 60) % 60, whole % 60,
                                                             (int) ((juce::jmax (0.0, secs) - whole) * 10.0)));
        resendAll = false;
    }

    void lamp (const juce::String& a, bool on) { value (a, on ? 1.0f : 0.0f); }

    void value (const juce::String& a, float v)
    {
        const auto s = juce::String (v, 4);
        if (! resendAll && sent[a] == s) return;
        sent[a] = s;
        sender.send (a, v);
    }

    void text (const juce::String& a, const juce::String& v)
    {
        if (! resendAll && sent[a] == v) return;
        sent[a] = v;
        sender.send (a, v);
    }

    TwainNode& twain;
    std::unique_ptr<juce::DatagramSocket> socket;
    std::atomic<int> port { 0 };
    juce::OSCSender sender;
    Settings settings;
    bool connected = false, resendAll = true, failed = false;
    juce::uint32 lastHeardMs = 0;
    juce::String lastFrom;
    int ticks = 0;
    juce::String surfaceHost;
    std::map<juce::String, juce::String> sent;

    JUCE_DECLARE_WEAK_REFERENCEABLE (OscControl)
};

} // namespace melville
