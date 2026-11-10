#pragma once

// TWAIN's headphone mixes on the players' phones: a small web server on the local network.
// Each player opens http://<this PC>:8770 on their phone, picks their feed and sets their own
// levels. Nothing to install. (Windows asks once whether MELVILLE may take connections.)

#include "LanAddress.h"
#include <juce_core/juce_core.h>
#include "../Core/TwainNode.h"

namespace melville
{

class PhonesServer : private juce::Thread
{
public:
    explicit PhonesServer (TwainNode& t) : juce::Thread ("TWAIN phones"), twain (t) {}
    ~PhonesServer() override { stop(); }

    bool start()
    {
        for (int p = 8770; p < 8780; ++p)
            if (listener.createListener (p)) { port = p; startThread(); return true; }
        return false;
    }

    void stop()
    {
        signalThreadShouldExit();
        listener.close();
        stopThread (2000);
    }

    /** What's cabled into each cue input (message thread, now and then). */
    void setCueNames (const juce::StringArray& names)
    {
        const juce::ScopedLock sl (lock);
        cueNames = names;
    }

    /** The address to open on a phone ("" while not running). */
    juce::String getUrl() const
    {
        if (port == 0) return {};
        return "http://" + lanAddress() + ":" + juce::String (port);
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            std::unique_ptr<juce::StreamingSocket> s (listener.waitForNextConnection());
            if (s == nullptr) { if (! threadShouldExit()) wait (50); continue; }
            handle (*s);
        }
    }

    void handle (juce::StreamingSocket& s)
    {
        // the request line (and headers, which we don't need)
        juce::MemoryBlock got;
        char buf[2048];
        const auto until = juce::Time::getMillisecondCounter() + 2000;
        while (juce::Time::getMillisecondCounter() < until)
        {
            if (s.waitUntilReady (true, 200) <= 0) continue;
            const int n = s.read (buf, (int) sizeof (buf), false);
            if (n <= 0) break;
            got.append (buf, (size_t) n);
            if (got.toString().contains ("\r\n\r\n") || got.getSize() > 16384) break;
        }
        const auto request = got.toString().upToFirstOccurrenceOf ("\r\n", false, false);
        const auto target = request.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
        const auto path = target.upToFirstOccurrenceOf ("?", false, false);
        juce::StringPairArray q;
        for (auto& kv : juce::StringArray::fromTokens (target.fromFirstOccurrenceOf ("?", false, false), "&", {}))
            q.set (kv.upToFirstOccurrenceOf ("=", false, false), juce::URL::removeEscapeChars (kv.fromFirstOccurrenceOf ("=", false, false)));

        if (path == "/state")      reply (s, "application/json", state (q["feed"].getIntValue()));
        else if (path == "/set")   { set (q); reply (s, "text/plain", "ok"); }
        else                       reply (s, "text/html; charset=utf-8", page());
    }

    static void reply (juce::StreamingSocket& s, const juce::String& type, const juce::String& body)
    {
        const auto bytes = body.toUTF8();
        const auto len = (int) std::strlen (bytes.getAddress());
        const auto head = "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + juce::String (len)
                          + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        s.write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
        s.write (bytes.getAddress(), len);
    }

    juce::String state (int feed)
    {
        feed = juce::jlimit (0, TwainNode::numFeeds - 1, feed);
        auto& fd = twain.feeds[(size_t) feed];
        juce::StringArray names;
        { const juce::ScopedLock sl (lock); names = cueNames; }
        juce::String j;
        j << "{\"feeds\":" << TwainNode::numFeeds << ",\"feed\":" << feed << ",\"cues\":[";
        for (int c = 0; c < TwainNode::numCues; ++c)
        {
            if (c > 0) j << ",";
            const auto name = c < names.size() ? names[c] : juce::String();
            j << "{\"name\":" << juce::JSON::toString (name) << ",\"level\":" << juce::String (fd.cue[c].load(), 3)
              << ",\"pan\":" << juce::String (fd.pan[c].load(), 2) << "}";
        }
        j << "],\"mix\":" << juce::String (fd.mixLevel.load(), 3) << ",\"click\":" << juce::String (fd.clickLevel.load(), 3)
          << ",\"level\":" << juce::String (fd.level.load(), 3) << ",\"peak\":" << juce::String (twain.feedPeak[(size_t) feed].load(), 3) << "}";
        return j;
    }

    void set (const juce::StringPairArray& q)
    {
        const int feed = q["feed"].getIntValue();
        if (! juce::isPositiveAndBelow (feed, TwainNode::numFeeds)) return;
        auto& fd = twain.feeds[(size_t) feed];
        const auto what = q["what"];
        const float v = (float) q["v"].getDoubleValue();
        const int i = q["i"].getIntValue();
        if (what == "level") fd.level = juce::jlimit (0.0f, 1.5f, v);
        else if (what == "mix") fd.mixLevel = juce::jlimit (0.0f, 1.0f, v);
        else if (what == "click") fd.clickLevel = juce::jlimit (0.0f, 1.0f, v);
        else if (what == "cue" && juce::isPositiveAndBelow (i, TwainNode::numCues)) fd.cue[i] = juce::jlimit (0.0f, 1.0f, v);
        else if (what == "pan" && juce::isPositiveAndBelow (i, TwainNode::numCues)) fd.pan[i] = juce::jlimit (-1.0f, 1.0f, v);
    }

    static juce::String page()
    {
        return R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>TWAIN phones</title>
<style>
:root{--o:#d9875a;--bg:#17181a;--plate:#26272a;--silk:#e9e2d2;--dim:#8f8a80}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--silk);font:15px/1.3 "Courier New",monospace}
header{position:sticky;top:0;background:linear-gradient(#303135,#1e1f22);padding:14px 16px 10px;border-bottom:2px solid var(--o);z-index:2}
h1{margin:0;font-size:18px;letter-spacing:2px;color:var(--o)}
h1 small{color:var(--dim);font-size:12px;letter-spacing:1px}
.feeds{display:grid;grid-template-columns:repeat(8,1fr);gap:6px;margin-top:10px}
.feeds button{background:#202124;color:var(--silk);border:1px solid #0e0f10;border-radius:5px;padding:9px 0;font:bold 14px "Courier New",monospace}
.feeds button.on{background:#7a4a30;color:#fff;box-shadow:0 0 8px var(--o) inset}
.row{display:grid;grid-template-columns:1fr 64px;align-items:center;gap:10px;padding:10px 16px;border-bottom:1px solid #222}
.name{font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.name b{color:var(--o)}
input[type=range]{width:100%;accent-color:var(--o);height:30px}
.val{text-align:right;color:var(--dim);font-size:12px}
.main{background:var(--plate);padding:14px 16px}
.main .name{font-size:15px;font-weight:bold}
.meter{height:8px;background:#0a0b0c;border-radius:4px;margin-top:8px;overflow:hidden}
.meter i{display:block;height:100%;width:0;background:linear-gradient(90deg,#5ccf5a 70%,#e8b02e 88%,#e8442e)}
.empty{color:var(--dim);padding:16px;font-size:13px}
</style></head><body>
<header><h1>TWAIN <small>&middot; HEADPHONES</small></h1><div class="feeds" id="feeds"></div></header>
<div class="main"><div class="row" style="padding:0;border:0"><div class="name">MY LEVEL</div><div class="val" id="lv"></div></div>
<input type="range" id="level" min="0" max="1.5" step="0.01"><div class="meter"><i id="meter"></i></div></div>
<div id="rows"></div>
<script>
let feed = +(localStorage.getItem('twainFeed') || 0), busy = 0, built = '';
const $ = id => document.getElementById(id);
const pct = v => Math.round(v * 100) + '%';
function send(what, v, i) { fetch(`/set?feed=${feed}&what=${what}&v=${v}` + (i !== undefined ? `&i=${i}` : '')); }
function slider(label, what, v, i, max) {
  const r = document.createElement('div'); r.className = 'row';
  r.innerHTML = `<div><div class="name">${label}</div><input type="range" min="0" max="${max||1}" step="0.01" value="${v}"></div><div class="val">${pct(v)}</div>`;
  const s = r.querySelector('input'), val = r.querySelector('.val');
  s.oninput = () => { busy = Date.now(); val.textContent = pct(+s.value); send(what, s.value, i); };
  s.dataset.key = what + (i ?? '');
  return r;
}
function feeds(n) {
  const f = $('feeds'); if (f.children.length === n) return;
  f.innerHTML = '';
  for (let k = 0; k < n; k++) {
    const b = document.createElement('button'); b.textContent = k + 1;
    b.onclick = () => { feed = k; localStorage.setItem('twainFeed', k); built = ''; poll(); };
    f.appendChild(b);
  }
}
async function poll() {
  try {
    const st = await (await fetch('/state?feed=' + feed)).json();
    feeds(st.feeds);
    [...$('feeds').children].forEach((b, k) => b.classList.toggle('on', k === st.feed));
    $('meter').style.width = Math.min(100, Math.max(0, (20 * Math.log10(st.peak + 1e-6) + 60) / 60 * 100)) + '%';
    if (Date.now() - busy < 1500) return;
    $('level').value = st.level; $('lv').textContent = pct(st.level);
    const shape = st.feed + '|' + st.cues.map(c => c.name).join('|');
    const rows = $('rows');
    if (shape !== built) {
      built = shape; rows.innerHTML = '';
      st.cues.forEach((c, i) => { if (c.name && !c.name.includes('nothing cabled')) rows.appendChild(slider(`<b>${i + 1}</b> ${c.name.replace(/^\d+\s+/, '')}`, 'cue', c.level, i)); });
      rows.appendChild(slider('THE MIX', 'mix', st.mix));
      rows.appendChild(slider('CLICK', 'click', st.click));
      if (rows.children.length === 2) rows.insertAdjacentHTML('afterbegin', '<div class="empty">Nothing is cabled into TWAIN\'s CUE IN jacks yet.</div>');
    } else {
      rows.querySelectorAll('input').forEach(s => {
        const k = s.dataset.key, v = k === 'mix' ? st.mix : k === 'click' ? st.click : st.cues[+k.slice(3)].level;
        s.value = v; s.closest('.row').querySelector('.val').textContent = pct(v);
      });
    }
  } catch (e) {}
}
$('level').oninput = e => { busy = Date.now(); $('lv').textContent = pct(+e.target.value); send('level', e.target.value); };
poll(); setInterval(poll, 600);
</script></body></html>)HTML";
    }

    TwainNode& twain;
    juce::StreamingSocket listener;
    int port = 0;
    juce::CriticalSection lock;
    juce::StringArray cueNames;
};

} // namespace melville
