#pragma once

// MELVILLE's control surfaces over OSC: the hardware TWAIN, the Hawthorne transport (and a meter
// bridge later), or TouchOSC pages standing in for them. Listens on a UDP port (8100) and answers
// every surface it has heard from (or a fixed list of addresses) on the reply port (9100), with
// every lamp and readout, so a surface's keys light like the hardware's LEDs. A surface that only
// listens should send /melville/sync now and then (every few seconds) so MELVILLE keeps answering
// it. Set up in MELVILLE's INTERFACE window. (IRVING's own OSC is on 9000 / 9001.)
//
// TWAIN
//   /twain/source/mix  /twain/source/2track  /twain/source/ext1  /twain/source/ext2     press
//   /twain/speakers/a  /twain/speakers/b  /twain/speakers/c                            press
//   /twain/dim  /twain/mute  /twain/mono                                     press: on / off
//   /twain/ref                                                 press: the level to 0 dB
//   /twain/level f       0..1 = -60 .. +6 dB (a knob)
//   /twain/level/step f  half-dB steps, + or - (what an encoder sends)
//   back: each key 1 / 0; /twain/level f; /twain/level/text s; /twain/display/1 s, /twain/display/2 s
//
// HAWTHORNE (its transport; without a reel, the play / stop / record keys run MELVILLE's clock)
//   /hawthorne/rtz  /stop  /play  /reverse  /rec                                        press
//   /hawthorne/rew  /hawthorne/ffw                         held: winds while it's held down
//   /hawthorne/mark                                        press: a marker here
//   /hawthorne/marker/prev  /hawthorne/marker/next         press: to the marker before / after
//   /hawthorne/in  /hawthorne/out                          press: the autolocator's IN / OUT here
//   /hawthorne/goto/in  /hawthorne/goto/out                press: go there
//   /hawthorne/loop  /hawthorne/auto  /hawthorne/rehearse  press: on / off
//   /hawthorne/locate/1 .. /8                              press: go to LOC n (or store it, after STORE)
//   /hawthorne/store                                       press: the next LOC key stores here
//   /hawthorne/store/1 .. /8                               press: store LOC n here
//   /hawthorne/vari/up  /hawthorne/vari/down               press: varispeed to the next whole semitone up / down
//   /hawthorne/save                                        press: save the reel
//   /hawthorne/end                                         press: to the end of what's recorded
//   /hawthorne/marker/delete                               press: delete the marker it's in (marker/now)
//   /hawthorne/click                                       press: MELVILLE's click on / off
//   back: each key 1 / 0 (LOC n lit: it holds a place; IN / OUT lit: set); /hawthorne/counter s;
//         /hawthorne/in/text s, /hawthorne/out/text s; /hawthorne/marker/now s, /hawthorne/marker/next s;
//         /hawthorne/beat s ("bar.beat", "12.3": Hawthorne's own, as its counter shows it - count-in bars are
//         negative, no bar 0; empty with no reel on the deck or a reel with no tempo);
//         /hawthorne/speed f (the deck's play speed setting, 1 = as recorded: varispeed and deck ips)
//
// /transport/... (rtz stop play rec rew ffw, counter): MELVILLE's transport, as on its bar
// /melville/sync                                        any surface: send me everything

#include <JuceHeader.h>
#include "../Core/TwainNode.h"
#include "../Core/ClockNode.h"
#include "../Hosted/MelvilleHosted.h"
#include "LanAddress.h"
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
        juce::String replyHost;                 // empty: answer every surface heard; else these addresses (comma separated)

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
        juce::StringArray fixedHosts() const
        {
            auto a = juce::StringArray::fromTokens (replyHost, ",; ", "");
            a.trim(); a.removeEmptyStrings();
            return a;
        }
    };

    /** The one MELVILLE runs (for its settings), or nullptr. */
    static OscControl*& instance() { static OscControl* o = nullptr; return o; }
    /** The settings changed (to save them). */
    std::function<void (const Settings&)> onSettingsChanged;
    /** MELVILLE's clock (its click), for the CLICK key. */
    ClockNode* clock = nullptr;

    explicit OscControl (TwainNode& t) : juce::Thread ("MELVILLE OSC"), twain (t) { instance() = this; }
    ~OscControl() override { stop(); if (instance() == this) instance() = nullptr; }

    /** Use these settings: stops, and starts again if it's on. Returns false if the port couldn't be opened. */
    bool apply (const Settings& s)
    {
        stop();
        settings = s;
        surfaces.clear();
        if (onSettingsChanged) onSettingsChanged (settings);
        if (! settings.enabled) return true;
        socket = std::make_unique<juce::DatagramSocket> (false);
        if (! socket->bindToPort (settings.listenPort)) { socket.reset(); failed = true; return false; }
        failed = false;
        port = settings.listenPort;
        for (auto& h : settings.fixedHosts()) surfaceFor (h);
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
        return port == 0 ? juce::String() : lanAddress() + ":" + juce::String (port);
    }

    /** One line for the settings: off, listening where, which surfaces. */
    juce::String getStatus() const
    {
        if (! settings.enabled) return "Off.";
        if (failed || port == 0) return "Port " + juce::String (settings.listenPort) + " is taken (by another program?): pick another.";
        juce::String s = "Listening on " + getAddress() + ". ";
        if (const auto more = lanAddresses(); more.size() > 1)
        {
            juce::StringArray rest (more);
            rest.remove (0);
            s << "(Also " << rest.joinIntoString (", ") << ".) ";
        }
        if (surfaces.empty()) return s + "No surfaces yet.";
        juce::StringArray list;
        const auto now = juce::Time::getMillisecondCounter();
        for (auto& [host, sf] : surfaces)
        {
            if (sf.lastHeard == 0) { list.add (host + " (fixed)"); continue; }
            const auto ago = (now - sf.lastHeard) / 1000u;
            list.add (host + (ago < 2 ? juce::String (" just now") : " " + juce::String ((int) ago) + " s ago"));
        }
        return s + "Answering on port " + juce::String (settings.replyPort) + ": " + list.joinIntoString (", ") + ".";
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
                    if (self->settings.fixedHosts().isEmpty() || self->surfaces.count (from) > 0)
                    {
                        auto& sf = self->surfaceFor (from);
                        sf.lastHeard = juce::jmax (1u, juce::Time::getMillisecondCounter());
                        sf.resend = true;            // (and put every lamp right, the pressed one too)
                    }
                    for (auto& m : msgs) self->handle (m);
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

    static void deck (int command, int arg = 0)
    {
        if (auto& d = melville::transport().deck) d (command, arg);
    }

    void handle (const Msg& m)
    {
        const auto& a = m.address;
        const bool press = ! m.hasValue || m.value > 0.5f;
        auto onPress = [press] (auto f) { if (press) f(); };

        // --- TWAIN
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
        if (a == "/twain/level/step" && m.hasValue && std::abs (m.value) > 0.0f)
        {
            twain.monitorLevel = dbGain (levelDb (twain.monitorLevel.load()) + 0.5f * m.value);
            return;
        }

        // --- the transport: MELVILLE's (/transport) and Hawthorne's (/hawthorne)
        const bool hw = a.startsWith ("/hawthorne/");
        if (hw || a.startsWith ("/transport/"))
        {
            const auto key = a.fromFirstOccurrenceOf ("/", false, false).fromFirstOccurrenceOf ("/", false, false);
            if (key == "play") { onPress ([] { melville::play(); });         return; }
            if (key == "stop") { onPress ([] { melville::stop(); });         return; }
            if (key == "rec")  { onPress ([] { melville::record(); });       return; }
            if (key == "rtz")  { onPress ([] { melville::returnToZero(); }); return; }
            if (key == "rew" || key == "ffw") { melville::wind (press ? (key == "rew" ? -1 : 1) : 0); return; }
        }
        if (! hw) return;
        const auto key = a.substring (11);           // (after "/hawthorne/")
        if (key == "reverse")      { onPress ([] { deck (deckReverse); });    return; }
        if (key == "mark")         { onPress ([] { deck (deckMark); });       return; }
        if (key == "marker/prev")  { onPress ([] { deck (deckPrevMarker); }); return; }
        if (key == "marker/next")  { onPress ([] { deck (deckNextMarker); }); return; }
        if (key == "in")           { onPress ([] { deck (deckSetIn); });      return; }
        if (key == "out")          { onPress ([] { deck (deckSetOut); });     return; }
        if (key == "goto/in")      { onPress ([] { deck (deckGotoIn); });     return; }
        if (key == "goto/out")     { onPress ([] { deck (deckGotoOut); });    return; }
        if (key == "loop")         { onPress ([] { deck (deckLoop); });       return; }
        if (key == "auto")         { onPress ([] { deck (deckAutoPunch); });  return; }
        if (key == "rehearse")     { onPress ([] { deck (deckRehearse); });   return; }
        if (key == "vari/up")      { onPress ([] { deck (deckSemitoneUp); });   return; }
        if (key == "vari/down")    { onPress ([] { deck (deckSemitoneDown); }); return; }
        if (key == "save")         { onPress ([] { deck (deckSave); });         return; }
        if (key == "end")          { onPress ([] { deck (deckGotoEnd); });      return; }
        if (key == "marker/delete"){ onPress ([] { deck (deckDeleteMarker); }); return; }
        if (key == "click")        { onPress ([this] { if (clock != nullptr) clock->clickOn = ! clock->clickOn.load(); }); return; }
        if (key == "store")        { onPress ([this] { storeArmed = ! storeArmed; }); return; }
        if (key.startsWith ("locate/") || key.startsWith ("store/"))
        {
            const int k = key.fromLastOccurrenceOf ("/", false, false).getIntValue() - 1;
            if (! juce::isPositiveAndBelow (k, 8)) return;
            onPress ([&]
            {
                const bool store = key.startsWith ("store/") || storeArmed;
                storeArmed = false;
                deck (store ? deckStoreLocate : deckLocate, k);
            });
            return;
        }
        // (/melville/sync and anything else: just the lamps)
    }

    //==========================================================================
    // answering (message thread)
    struct Surface
    {
        std::unique_ptr<juce::OSCSender> sender = std::make_unique<juce::OSCSender>();
        bool connected = false, resend = true;
        juce::uint32 lastHeard = 0;                  // 0: a fixed address
        std::map<juce::String, juce::String> sent;
    };

    Surface& surfaceFor (const juce::String& host)
    {
        auto& sf = surfaces[host];
        if (! sf.connected) sf.connected = sf.sender->connect (host, settings.replyPort);
        return sf;
    }

    struct Out { juce::String address; bool isText; float v; juce::String text; };

    static juce::String clockText (double secs, bool tenths)
    {
        const double s = juce::jmax (0.0, secs);
        const int whole = (int) std::floor (s);
        return tenths ? juce::String::formatted ("%02d:%02d:%02d.%d", whole / 3600, (whole / 60) % 60, whole % 60, (int) ((s - whole) * 10.0))
                      : juce::String::formatted ("%02d:%02d.%d", whole / 60, whole % 60, (int) ((s - whole) * 10.0));
    }

    /** Everything a surface shows, now. */
    std::vector<Out> state() const
    {
        std::vector<Out> o;
        auto lamp = [&o] (const juce::String& a, bool on) { o.push_back ({ a, false, on ? 1.0f : 0.0f, {} }); };
        auto text = [&o] (const juce::String& a, const juce::String& t) { o.push_back ({ a, true, 0.0f, t }); };

        // TWAIN
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
        const float db = levelDb (twain.monitorLevel.load());
        lamp ("/twain/ref", std::abs (db) < 0.25f);
        o.push_back ({ "/twain/level", false, (db + 60.0f) / 66.0f, {} });
        text ("/twain/level/text", twain.mute.load() ? juce::String ("MUTE") : db <= -59.9f ? juce::String ("-INF") : juce::String (db, 1) + " dB");
        static const char* names[] = { "MIX", "2-TRACK", "EXT 1", "EXT 2" };
        const juce::String listening = juce::isPositiveAndBelow (src, 4) ? juce::String (names[src]) : "PHONES " + juce::String (src - TwainNode::feed1 + 1);
        text ("/twain/display/1", "LISTENING  " + listening + "   SPEAKERS " + juce::String::charToString ((juce::juce_wchar) ('A' + spk)));
        juce::StringArray flags;
        if (twain.dim.load())  flags.add ("DIM");
        if (twain.mute.load()) flags.add ("MUTE");
        if (twain.mono.load()) flags.add ("MONO");
        text ("/twain/display/2", flags.isEmpty() ? juce::String ("MELVILLE  TWAIN") : flags.joinIntoString ("  "));

        // the transport
        auto& t = melville::transport();
        const int lamps = t.deckLamps.load();
        const bool rolling = melville::isRolling(), recording = t.recording.load(), reversing = (lamps & lampReversing) != 0;
        const double rate = juce::jmax (1.0, t.reelRate.load());
        const auto counter = clockText ((double) t.position.load() / rate, true);
        for (auto* ns : { "/transport/", "/hawthorne/" })
        {
            const juce::String n (ns);
            lamp (n + "play", rolling && ! reversing);
            lamp (n + "rec", recording);
            lamp (n + "stop", ! rolling);
            text (n + "counter", counter);
        }
        lamp ("/hawthorne/reverse", reversing);
        lamp ("/hawthorne/loop", (lamps & lampLoop) != 0);
        lamp ("/hawthorne/auto", (lamps & lampAutoPunch) != 0);
        lamp ("/hawthorne/rehearse", (lamps & lampRehearse) != 0);
        const auto in = t.punchIn.load(), out = t.punchOut.load();
        lamp ("/hawthorne/in", in >= 0);
        lamp ("/hawthorne/out", out >= 0);
        text ("/hawthorne/in/text", in >= 0 ? clockText ((double) in / rate, false) : juce::String ("--:--"));
        text ("/hawthorne/out/text", out >= 0 ? clockText ((double) out / rate, false) : juce::String ("--:--"));
        const int stored = t.locatesStored.load();
        for (int k = 0; k < 8; ++k) lamp ("/hawthorne/locate/" + juce::String (k + 1), (stored >> k) & 1);
        lamp ("/hawthorne/store", storeArmed);
        lamp ("/hawthorne/click", clock != nullptr && clock->clickOn.load());
        juce::String now, next;
        {
            const juce::SpinLock::ScopedLockType sl (t.markerLock);
            now = t.markerNow; next = t.markerNext;
        }
        // bar . beat: straight from Hawthorne's counter; nothing with no reel on the deck or no tempo on it
        text ("/hawthorne/beat", t.barBeatState.load() == 1 ? juce::String (t.bar.load()) + "." + juce::String (t.beat.load())
                                                            : juce::String());
        // the deck's speed setting (varispeed)
        o.push_back ({ "/hawthorne/speed", false, (float) (std::round (t.speed.load() * 10000.0) / 10000.0), {} });
        text ("/hawthorne/marker/now", now.isEmpty() ? juce::String ("-") : now.toUpperCase());
        text ("/hawthorne/marker/next", next.isEmpty() ? juce::String ("-") : next.toUpperCase());
        return o;
    }

    void timerCallback() override
    {
        if (surfaces.empty()) return;
        const bool everything = ++ticks % 30 == 0;   // (every two seconds: a surface that's just woken up)
        const auto now = state();
        const auto clock = juce::Time::getMillisecondCounter();
        for (auto it = surfaces.begin(); it != surfaces.end();)
        {
            auto& sf = it->second;
            if (sf.lastHeard != 0 && clock - sf.lastHeard > 60u * 60u * 1000u) { it = surfaces.erase (it); continue; }   // (an hour's silence: gone)
            if (sf.connected)
                for (auto& o : now)
                {
                    const auto v = o.isText ? o.text : juce::String (o.v, 4);
                    if (! (everything || sf.resend) && sf.sent[o.address] == v) continue;
                    sf.sent[o.address] = v;
                    if (o.isText) sf.sender->send (o.address, o.text); else sf.sender->send (o.address, o.v);
                }
            sf.resend = false;
            ++it;
        }
    }

    TwainNode& twain;
    std::unique_ptr<juce::DatagramSocket> socket;
    std::atomic<int> port { 0 };
    Settings settings;
    bool failed = false, storeArmed = false;
    int ticks = 0;
    std::map<juce::String, Surface> surfaces;

    JUCE_DECLARE_WEAK_REFERENCEABLE (OscControl)
};

} // namespace melville
