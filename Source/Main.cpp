// MELVILLE: Hawthorne, EMERSON, WHITMAN, THOREAU and PEQUOD Host as one program.
//
// MELVILLE owns the audio interface and runs every part from its own audio callback,
// with a patchbay between them (Core/Hub.h). Each part keeps its own window and look;
// the MELVILLE bar opens them, the patchbay and the interface settings. Closing a
// part's window only hides it: the part keeps running. Closing the bar quits.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Core/Hub.h"
#include "Parts/Parts.h"
#include "UI/MelvilleLookAndFeel.h"
#include "UI/PatchbayView.h"
#include "UI/Taskbar.h"
#include "UI/WindowFrame.h"
#include "Core/NetworkNode.h"
#include "Updater.h"
#include "Hosted/MelvilleHosted.h"

namespace melville
{

//==============================================================================
/** A part's own window. */
static juce::Image loadIcon (const juce::String& id, int px = 128)
{
    const void* data = nullptr; int size = 0;
    if (id == "hawthorne") { data = BinaryData::HAWTHORNE_png;     size = BinaryData::HAWTHORNE_pngSize; }
    if (id == "emerson")   { data = BinaryData::EMERSON_png;       size = BinaryData::EMERSON_pngSize; }
    if (id == "whitman")   { data = BinaryData::WHITMAN_png;       size = BinaryData::WHITMAN_pngSize; }
    if (id == "thoreau")   { data = BinaryData::THOREAU_png;       size = BinaryData::THOREAU_pngSize; }
    if (id.startsWith ("pequod")) { data = BinaryData::IRVING_png;        size = BinaryData::IRVING_pngSize; }      // IRVING
    if (id == "melville")  { data = BinaryData::MELVILLE_png;      size = BinaryData::MELVILLE_pngSize; }
    if (data == nullptr) return {};
    // scaled down once, so the bar draws it crisply and cheaply
    auto big = juce::ImageCache::getFromMemory (data, size);
    return big.isValid() ? big.rescaled (px, px, juce::Graphics::highResamplingQuality) : big;
}

/** One of MELVILLE's own windows (patchbay, interface): closing hides it. */
class ToolWindow : public juce::DocumentWindow
{
public:
    explicit ToolWindow (const juce::String& name) : DocumentWindow (name, Colours::bg, DocumentWindow::allButtons) {}
    void closeButtonPressed() override { setVisible (false); }
};

class PartWindow : public juce::DocumentWindow
{
public:
    explicit PartWindow (PartUI& p)
        : DocumentWindow ("MELVILLE \xc2\xb7 " + p.title, p.background, DocumentWindow::allButtons), part (p)
    {
        if (part.lookAndFeel != nullptr) setLookAndFeel (part.lookAndFeel.get());
        // MELVILLE's own window bar, in the part's colour
        frameWindow (*this, part.content.get(), false, part.title, Colours::forPart (part.id), loadIcon (part.id, 64));
        setResizable (true, false);
        setResizeLimits (part.minWidth, part.minHeight + WindowTitleBar::height, 10000, 10000);
        centreWithSize (juce::jmax (getWidth(), part.minWidth), juce::jmax (getHeight(), part.minHeight));
    }

    ~PartWindow() override
    {
        clearContentComponent();
        setLookAndFeel (nullptr);
    }

    void show()
    {
        if (! isVisible())
        {
            setVisible (true);
            if (part.fullScreen && ! opened) setFullScreen (true);
            opened = true;
        }
        if (isMinimised()) setMinimised (false);
        toFront (true);
    }

    // closing a part sends it down to its taskbar button (it keeps running and keeps its place)
    void closeButtonPressed() override { setMinimised (true); }

private:
    PartUI& part;
    bool opened = false;
};

//==============================================================================
/** One tile on the MELVILLE bar: a part's own icon (or a drawn one), its name, and a lamp. */
class Tile : public juce::Button
{
public:
    Tile (const juce::String& name, juce::Image icon, juce::Colour colour)
        : juce::Button (name), image (std::move (icon)), accent (colour)
    {
        setTooltip (name);
    }

    std::function<void (juce::Graphics&, juce::Rectangle<float>)> drawIcon;   // when there's no image
    std::function<bool()> isLit;                                               // the lamp (none if empty)
    std::function<std::vector<bool>()> lamps;                                  // or a lamp per instance (IRVING)
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        juce::Button::mouseDown (e);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        // greyed out when there's nothing to do (UPDATE with no update)
        if (! isEnabled()) { g.beginTransparencyLayer (0.3f); over = down = false; }
        paintTile (g, over, down);
        if (! isEnabled()) g.endTransparencyLayer();
    }

    void paintTile (juce::Graphics& g, bool over, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        if (over || down)
        {
            g.setColour (Colours::panelHi.brighter (down ? 0.08f : 0.0f));
            g.fillRoundedRectangle (r, 6.0f);
        }

        auto iconArea = r.removeFromTop (r.getHeight() - 26.0f).reduced (6.0f);
        const float side = juce::jmin (iconArea.getWidth(), iconArea.getHeight());
        iconArea = iconArea.withSizeKeepingCentre (side, side);
        if (image.isValid())
            g.drawImage (image, iconArea, juce::RectanglePlacement::centred);
        else if (drawIcon)
            drawIcon (g, iconArea);

        g.setColour (over ? accent.brighter (0.2f) : accent);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawFittedText (getName().toUpperCase(), r.removeFromTop (14.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        if (lamps)
        {
            const auto on = lamps();
            auto row = r.removeFromTop (10.0f);
            const float pitch = 10.0f, x0 = row.getCentreX() - pitch * (float) (on.size() - 1) * 0.5f;
            for (size_t k = 0; k < on.size(); ++k)
            {
                g.setColour (on[k] ? accent : Colours::lampOff);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x0 + pitch * (float) k, row.getCentreY() }));
            }
        }
        else if (isLit)
        {
            const bool on = isLit();
            auto lamp = r.removeFromTop (10.0f).withSizeKeepingCentre (6.0f, 6.0f);
            g.setColour (on ? accent : Colours::lampOff);
            g.fillEllipse (lamp);
        }
    }

private:
    juce::Image image;
    juce::Colour accent;
};


//==============================================================================
/** MELVILLE's transport on the bar: RTZ, REW, FFW, STOP, PLAY, REC, the counter and the
    tempo. It drives Hawthorne (the clock), whose own keys show the same thing. */
class TransportStrip : public juce::Component, private juce::Timer
{
public:
    TransportStrip()
    {
        for (int k = 0; k < numKeys; ++k)
        {
            auto* b = keys.add (new Key ((Kind) k));
            addAndMakeVisible (b);
        }
        keys[rtz]->onClick  = [] { if (auto& f = melville::transport().returnToZero) f(); };
        keys[stop]->onClick = [] { if (auto& f = melville::transport().stop) f(); };
        keys[play]->onClick = [] { if (auto& f = melville::transport().play) f(); };
        keys[rec]->onClick  = [] { if (auto& f = melville::transport().record) f(); };
        for (int k : { (int) rew, (int) ffw })
        {
            auto* b = keys[k];
            b->onStateChange = [b, k, held = false] () mutable
            {
                auto& f = melville::transport().wind;
                if (f == nullptr) return;
                if (b->isDown() && ! held)       { held = true;  f (k == rew ? -1 : +1); }
                else if (! b->isDown() && held)  { held = false; f (0); }
            };
        }
        keys[rtz]->setTooltip ("Return to zero");
        keys[rew]->setTooltip ("Rewind (hold)");
        keys[ffw]->setTooltip ("Fast forward (hold)");
        keys[stop]->setTooltip ("Stop");
        keys[play]->setTooltip ("Play");
        keys[rec]->setTooltip ("Record (the armed tracks on Hawthorne)");

        tempo.setEditable (false, true, false);
        tempo.setJustificationType (juce::Justification::centred);
        tempo.setColour (juce::Label::textColourId, Colours::text);
        tempo.setColour (juce::Label::backgroundWhenEditingColourId, Colours::panelHi);
        tempo.setColour (juce::Label::textWhenEditingColourId, Colours::text);
        tempo.setTooltip ("MELVILLE's tempo (kept on Hawthorne's reel): double-click to type one, right-click for beats per bar and bar 1. "
                          "THOREAU and IRVING follow it.");
        tempo.addMouseListener (this, false);
        tempo.onTextChange = [this]
        {
            const double b = tempo.getText().retainCharacters ("0123456789.").getDoubleValue();
            if (b >= 20.0 && b <= 400.0)
                {
                    auto& t = melville::transport();
                    if (auto& f = t.setTempo) f (b, t.beatsPerBar.load(), t.barOne.load());
                }
            refresh();
        };
        addAndMakeVisible (tempo);
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        auto& t = melville::transport();
        // the counter, like a tape machine's: hours, minutes, seconds, frames
        g.setColour (juce::Colour (0xff101112));
        g.fillRoundedRectangle (counterArea.toFloat(), 4.0f);
        const double rate = juce::jmax (1.0, t.reelRate.load());
        const double secs = (double) t.position.load() / rate;
        const int h = (int) (secs / 3600.0), m = (int) std::fmod (secs / 60.0, 60.0), sec = (int) std::fmod (secs, 60.0);
        const int fr = (int) (std::fmod (secs, 1.0) * 30.0);
        const auto text = juce::String (h) + ":" + juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (sec).paddedLeft ('0', 2)
                          + ":" + juce::String (fr).paddedLeft ('0', 2);
        g.setColour (t.recording.load() ? Colours::warn : t.rolling.load() ? Colours::accent : Colours::text);
        g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        auto c = counterArea.reduced (8, 2);
        g.drawText (text, c.removeFromTop (c.getHeight() * 3 / 5), juce::Justification::centred);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (Colours::textDim);
        juce::String sub = t.available.load() ? juce::String ("TAPE") : juce::String ("NO HAWTHORNE");
        if (t.bpm.load() > 0.0)
        {
            const double beats = ((double) t.position.load() - t.barOne.load()) / (rate * 60.0 / t.bpm.load());
            const int bpb = juce::jmax (1, t.beatsPerBar.load());
            // as Hawthorne counts: ... bar -2, bar -1 (the count-in), bar 1, bar 2 ... (no bar 0)
            const auto b = (juce::int64) std::floor (beats + 1.0e-9);
            const auto barIdx = b >= 0 ? b / bpb : -((-b + bpb - 1) / bpb);
            const int bar = (int) (barIdx + (barIdx >= 0 ? 1 : 0)), beat = (int) (b - barIdx * bpb) + 1;
            sub = "BAR " + juce::String (bar) + "  BEAT " + juce::String (beat);
        }
        g.drawText (sub, c, juce::Justification::centred);

        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.setColour (Colours::textDim);
        g.drawText (t.bpm.load() > 0.0 ? "TEMPO  " + juce::String (t.beatsPerBar.load()) + "/4" : juce::String ("TEMPO"),
                    tempoCaption, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && ! counterArea.contains (e.getEventRelativeTo (this).getPosition())) return;
        auto& t = melville::transport();
        if (! t.available.load() || ! t.setTempo) return;
        const bool have = t.reelLoaded.load();
        const double bpm = t.bpm.load() > 0.0 ? t.bpm.load() : 120.0;
        const int bpb = juce::jmax (1, t.beatsPerBar.load());
        auto set = [] (double b, int n, double one) { auto& tt = melville::transport(); if (auto& f = tt.setTempo) f (b, n, juce::jmax (0.0, one)); };

        juce::PopupMenu m, sig;
        for (int n = 2; n <= 12; ++n)
            sig.addItem (juce::String (n) + "/4", have, n == bpb, [set, bpm, n] { set (bpm, n, melville::transport().barOne.load()); });
        m.addSubMenu ("Beats per bar", sig, have);
        m.addSeparator();
        m.addItem ("Bar 1 at the start of the tape", have && t.barOne.load() != 0.0, false, [set, bpm, bpb] { set (bpm, bpb, 0.0); });
        m.addItem ("Bar 1 here (where the tape is now)", have, false, [set, bpm, bpb]
                   { set (bpm, bpb, std::round ((double) melville::transport().position.load())); });
        const double ms = juce::jmax (1.0, t.reelRate.load()) * 0.001;
        m.addItem ("Bar 1 1 ms earlier", have, false, [set, bpm, bpb, ms] { set (bpm, bpb, melville::transport().barOne.load() - ms); });
        m.addItem ("Bar 1 1 ms later",   have, false, [set, bpm, bpb, ms] { set (bpm, bpb, melville::transport().barOne.load() + ms); });
        if (! have) { m.addSeparator(); m.addItem ("(load a reel on Hawthorne: the tempo lives on the reel)", false, false, nullptr); }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 18);
        for (auto* k : keys) { k->setBounds (r.removeFromLeft (40)); r.removeFromLeft (4); }
        r.removeFromLeft (8);
        counterArea = r.removeFromLeft (150);
        r.removeFromLeft (8);
        auto tr = r.removeFromLeft (80);
        tempoCaption = tr.removeFromTop (14);
        tempo.setBounds (tr);
    }

    static constexpr int preferredWidth = 6 * 44 + 8 + 150 + 8 + 80;

private:
    enum Kind { rtz, rew, ffw, stop, play, rec, numKeys };

    struct Key : juce::Button
    {
        explicit Key (Kind k) : juce::Button ({}), kind (k) {}
        Kind kind;
        bool lit = false;
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Colours::panelHi.brighter (down ? 0.12f : over ? 0.05f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);
            const auto on = kind == rec ? Colours::warn : Colours::accent;
            g.setColour (lit ? on : Colours::line);
            g.drawRoundedRectangle (r, 4.0f, lit ? 1.5f : 1.0f);

            auto a = r.withSizeKeepingCentre (16.0f, 16.0f);
            g.setColour (lit ? on : kind == rec ? Colours::warn.withAlpha (0.8f) : Colours::text);
            juce::Path p;
            auto tri = [&p] (juce::Rectangle<float> b, bool right)
            {
                if (right) p.addTriangle (b.getX(), b.getY(), b.getX(), b.getBottom(), b.getRight(), b.getCentreY());
                else       p.addTriangle (b.getRight(), b.getY(), b.getRight(), b.getBottom(), b.getX(), b.getCentreY());
            };
            switch (kind)
            {
                case rtz:  p.addRectangle (a.getX(), a.getY(), 3.0f, a.getHeight()); tri (a.withTrimmedLeft (4.0f), false); break;
                case rew:  tri (a.withWidth (8.0f), false); tri (a.withTrimmedLeft (8.0f), false); break;
                case ffw:  tri (a.withWidth (8.0f), true);  tri (a.withTrimmedLeft (8.0f), true); break;
                case stop: p.addRectangle (a.reduced (1.5f)); break;
                case play: tri (a.reduced (1.0f, 0.0f), true); break;
                case rec:  p.addEllipse (a.reduced (1.0f)); break;
                default: break;
            }
            g.fillPath (p);
        }
    };

    void refresh()
    {
        auto& t = melville::transport();
        const auto txt = t.bpm.load() > 0.0 ? juce::String (t.bpm.load(), 1) : juce::String ("--");
        if (! tempo.isBeingEdited() && tempo.getText() != txt) tempo.setText (txt, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        auto& t = melville::transport();
        const bool rolling = t.rolling.load(), recording = t.recording.load();
        if (keys[play]->lit != rolling)  { keys[play]->lit = rolling;  keys[play]->repaint(); }
        if (keys[rec]->lit != recording) { keys[rec]->lit = recording; keys[rec]->repaint(); }
        for (auto* k : keys) k->setEnabled (t.available.load());
        refresh();
        repaint (counterArea);
    }

    juce::OwnedArray<Key> keys;
    juce::Label tempo;
    juce::Rectangle<int> counterArea, tempoCaption;
};

//==============================================================================
/** INTERFACE: the device selector, and underneath a round-trip latency measurement. */
class InterfacePanel : public juce::Component, private juce::Timer
{
public:
    explicit InterfacePanel (Hub& h) : hub (h), selector (h.deviceManager, 0, 64, 0, 64, true, false, false, false)
    {
        addAndMakeVisible (selector);
        caption.setText ("LATENCY", juce::dontSendNotification);
        caption.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        caption.setColour (juce::Label::textColourId, Colours::accent);
        addAndMakeVisible (caption);
        help.setText ("Cable an output of the interface straight into an input, pick them, and MEASURE: MELVILLE times a few clicks "
                      "round the loop. What the driver doesn't report is added to every part's latency, so overdubs land in time. "
                      "Take the cable out afterwards.", juce::dontSendNotification);
        help.setJustificationType (juce::Justification::topLeft);
        help.setColour (juce::Label::textColourId, Colours::textDim);
        addAndMakeVisible (help);
        for (auto* b : { &outBox, &inBox }) addAndMakeVisible (b);
        outBox.setTooltip ("The output the loop cable comes from");
        inBox.setTooltip ("The input the loop cable goes into");
        measure.onClick = [this] { start(); };
        measure.setTooltip ("Send clicks out and time them back (the output plays only the clicks for a moment)");
        addAndMakeVisible (measure);
        clear.onClick = [this] { hub.extraLatency = 0; result.setText ("cleared: the driver's figures only", juce::dontSendNotification); };
        clear.setTooltip ("Forget the measurement");
        addAndMakeVisible (clear);
        result.setColour (juce::Label::textColourId, Colours::text);
        addAndMakeVisible (result);
        fillBoxes();
        showCurrent();
        setSize (580, 640);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        selector.setBounds (r.removeFromTop (440));
        r.reduce (16, 6);
        caption.setBounds (r.removeFromTop (22));
        help.setBounds (r.removeFromTop (52));
        r.removeFromTop (6);
        auto row = r.removeFromTop (28);
        outBox.setBounds (row.removeFromLeft (150)); row.removeFromLeft (8);
        inBox.setBounds (row.removeFromLeft (150)); row.removeFromLeft (8);
        measure.setBounds (row.removeFromLeft (100)); row.removeFromLeft (8);
        clear.setBounds (row.removeFromLeft (70));
        r.removeFromTop (8);
        result.setBounds (r.removeFromTop (44));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::bg);
        g.setColour (Colours::line);
        g.drawHorizontalLine (440, 12.0f, (float) getWidth() - 12.0f);
    }

private:
    void fillBoxes()
    {
        outBox.clear (juce::dontSendNotification);
        inBox.clear (juce::dontSendNotification);
        if (auto* d = hub.deviceManager.getCurrentAudioDevice())
        {
            const auto outs = d->getOutputChannelNames(), ins = d->getInputChannelNames();
            const auto activeOut = d->getActiveOutputChannels(), activeIn = d->getActiveInputChannels();
            for (int c = 0; c < outs.size(); ++c) if (activeOut[c]) outBox.addItem ("Out " + juce::String (c + 1) + "  " + outs[c], c + 1);
            for (int c = 0; c < ins.size(); ++c)  if (activeIn[c])  inBox.addItem ("In " + juce::String (c + 1) + "  " + ins[c], c + 1);
        }
        outBox.setSelectedItemIndex (0, juce::dontSendNotification);
        inBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }

    void showCurrent()
    {
        auto* d = hub.deviceManager.getCurrentAudioDevice();
        const int rep = d != nullptr ? d->getInputLatencyInSamples() + d->getOutputLatencyInSamples() : 0;
        const int extra = hub.extraLatency.load();
        result.setText ("Driver reports " + juce::String (rep) + " samples round trip"
                        + (extra != 0 ? ";  measured extra " + juce::String (extra) + " samples (" + ms (extra) + ")" : juce::String ("; not measured")),
                        juce::dontSendNotification);
    }

    juce::String ms (int samples) const { return juce::String (1000.0 * samples / juce::jmax (1.0, hub.getSampleRate()), 2) + " ms"; }

    void start()
    {
        if (outBox.getSelectedId() == 0 || inBox.getSelectedId() == 0) { result.setText ("No interface open", juce::dontSendNotification); return; }
        // the device's channel index among the active ones (that's how the hub sees them)
        auto indexOf = [] (const juce::BigInteger& active, int channel) { int k = 0; for (int c = 0; c < channel; ++c) k += active[c] ? 1 : 0; return k; };
        auto* d = hub.deviceManager.getCurrentAudioDevice();
        if (d == nullptr) return;
        hub.startLatencyTest (indexOf (d->getActiveOutputChannels(), outBox.getSelectedId() - 1),
                              indexOf (d->getActiveInputChannels(), inBox.getSelectedId() - 1));
        result.setText ("measuring...", juce::dontSendNotification);
        measure.setEnabled (false);
        startTimer (100);
    }

    void timerCallback() override
    {
        auto& t = hub.getLatencyTest();
        const int s = t.state.load();
        if (s == Hub::LatencyTest::running) return;
        stopTimer();
        measure.setEnabled (true);
        if (s == Hub::LatencyTest::done)
        {
            const int extra = juce::jmax (0, t.roundTrip.load() - t.reported.load());
            hub.extraLatency = extra;
            result.setText ("Round trip " + juce::String (t.roundTrip.load()) + " samples (" + ms (t.roundTrip.load()) + "); driver reports "
                            + juce::String (t.reported.load()) + ".  Extra " + juce::String (extra) + " samples now added for every part.",
                            juce::dontSendNotification);
        }
        else
            result.setText ("Nothing came back: check the cable goes from that output to that input, and the input level isn't down.",
                            juce::dontSendNotification);
        t.state = Hub::LatencyTest::idle;
    }

    Hub& hub;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label caption, help, result;
    juce::ComboBox outBox, inBox;
    juce::TextButton measure { "MEASURE" }, clear { "CLEAR" };
};

/** The MELVILLE bar: every part's icon (click to bring up its window, lamp lit while it
    runs), then the patchbay and the interface. It stays on top of the parts' windows. */
class Bar : public juce::Component, private juce::Timer
{
public:
    Bar (Hub& h, std::vector<std::unique_ptr<PartUI>>& p, std::vector<std::unique_ptr<PartWindow>>& w)
        : hub (h), parts (p), windows (w)
    {
        rebuildTiles();

        patchTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // a jack field with two cords
            g.setColour (Colours::panelHi);
            g.fillRoundedRectangle (r, r.getWidth() * 0.12f);
            const float step = r.getWidth() / 5.0f;
            g.setColour (Colours::textDim);
            for (int row = 0; row < 2; ++row)
                for (int c = 0; c < 4; ++c)
                    g.fillEllipse (r.getX() + step * (c + 1) - 3.5f, r.getY() + r.getHeight() * (row == 0 ? 0.3f : 0.7f) - 3.5f, 7.0f, 7.0f);
            g.setColour (Colours::accent);
            juce::Path cord;
            cord.startNewSubPath (r.getX() + step, r.getY() + r.getHeight() * 0.3f);
            cord.cubicTo (r.getX() + step, r.getBottom(), r.getX() + step * 3, r.getY(), r.getX() + step * 3, r.getY() + r.getHeight() * 0.7f);
            cord.startNewSubPath (r.getX() + step * 2, r.getY() + r.getHeight() * 0.3f);
            cord.cubicTo (r.getX() + step * 2, r.getBottom() + 4.0f, r.getX() + step * 4, r.getY() + 4.0f, r.getX() + step * 4, r.getY() + r.getHeight() * 0.7f);
            g.strokePath (cord, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        audioTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // an interface: a box with two inputs and a meter
            auto box = r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.56f);
            g.setColour (Colours::panelHi);
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (Colours::textDim);
            for (int c = 0; c < 2; ++c)
                g.drawEllipse (box.getX() + box.getWidth() * (0.18f + 0.22f * c) - 7.0f, box.getCentreY() - 7.0f, 14.0f, 14.0f, 2.0f);
            g.setColour (Colours::accent);
            for (int k = 0; k < 5; ++k)
                g.fillRect (box.getX() + box.getWidth() * 0.66f + k * 6.0f, box.getCentreY() + 8.0f - k * 4.0f, 4.0f, 4.0f + k * 4.0f);
        };
        netTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // three machines linked
            const juce::Point<float> a { r.getCentreX(), r.getY() + r.getHeight() * 0.22f },
                                     b { r.getX() + r.getWidth() * 0.2f, r.getBottom() - r.getHeight() * 0.22f },
                                     c { r.getRight() - r.getWidth() * 0.2f, r.getBottom() - r.getHeight() * 0.22f };
            g.setColour (Colours::textDim);
            for (auto [p, q] : { std::pair { a, b }, std::pair { a, c }, std::pair { b, c } }) g.drawLine ({ p, q }, 2.0f);
            for (auto p : { a, b, c })
            {
                g.setColour (Colours::panelHi);
                g.fillRoundedRectangle (juce::Rectangle<float> (16, 12).withCentre (p), 2.0f);
                g.setColour (Colours::forPart ("network"));
                g.drawRoundedRectangle (juce::Rectangle<float> (16, 12).withCentre (p), 2.0f, 1.5f);
            }
        };
        sessionTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // a tape box: the whole studio in one
            auto box = r.reduced (r.getWidth() * 0.14f, r.getHeight() * 0.08f);
            g.setColour (Colours::panelHi);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (Colours::textDim);
            g.drawRoundedRectangle (box, 3.0f, 1.5f);
            const float d = box.getWidth() * 0.62f;
            auto reelArea = juce::Rectangle<float> (d, d).withCentre ({ box.getCentreX(), box.getY() + box.getHeight() * 0.42f });
            g.setColour (Colours::accent);
            g.drawEllipse (reelArea, 2.5f);
            g.fillEllipse (reelArea.withSizeKeepingCentre (d * 0.22f, d * 0.22f));
            g.fillRect (box.getX() + box.getWidth() * 0.2f, box.getBottom() - box.getHeight() * 0.18f, box.getWidth() * 0.6f, 3.0f);
        };
        sessionTile.onClick = [this] { if (onSessionMenu) onSessionMenu (sessionTile); };
        sessionTile.setTooltip ("SESSION: save or open the whole studio in one file (the reel, racks, mix, drums, IRVINGs and patch)");
        addAndMakeVisible (sessionTile);

        netTile.onClick = [this] { if (onShowNetwork) onShowNetwork(); };
        netTile.setTooltip ("MELVILLE LINK: audio from other PCs (THOREAU, IRVING), onto the patchbay");
        addAndMakeVisible (netTile);
        updateTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // a download arrow into a tray
            auto box = r.reduced (r.getWidth() * 0.12f);
            g.setColour (Colours::panelHi);
            g.fillRoundedRectangle (box, box.getWidth() * 0.14f);
            g.setColour (Colours::accent);
            const float cx = box.getCentreX(), top = box.getY() + box.getHeight() * 0.2f, tip = box.getY() + box.getHeight() * 0.62f;
            g.drawLine (cx, top, cx, tip, 3.0f);
            juce::Path head;
            head.addTriangle (cx - 8.0f, tip - 7.0f, cx + 8.0f, tip - 7.0f, cx, tip + 3.0f);
            g.fillPath (head);
            const float tray = box.getY() + box.getHeight() * 0.8f;
            g.drawLine (box.getX() + box.getWidth() * 0.22f, tray, box.getRight() - box.getWidth() * 0.22f, tray, 3.0f);
        };
        updateTile.onClick = [this] { offerUpdate(); };
        addAndMakeVisible (updateTile);
        updater.onChange = [this] { refreshUpdate(); };
        updater.onStatus = [] (const juce::String& msg)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MELVILLE update", msg);
        };
        refreshUpdate();
        updater.checkSoon();

        patchTile.onClick = [this] { showPatchbay(); };
        audioTile.onClick = [this] { showAudio(); };
        patchTile.setTooltip ("PATCHBAY: what goes where between the parts and the interface");
        audioTile.setTooltip ("INTERFACE: the audio interface, sample rate and buffer");
        addAndMakeVisible (patchTile);
        addAndMakeVisible (audioTile);
        addAndMakeVisible (transportStrip);

        startTimerHz (4);
        setSize (wordmarkWidth + (tiles.size() + 5) * (tileWidth + gap) + 26 + TransportStrip::preferredWidth + 28 + 200, barHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::panel);
        auto r = getLocalBounds().reduced (14, 0);
        g.setColour (Colours::accent);
        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
        g.drawText ("MELVILLE", r.removeFromLeft (wordmarkWidth - 14), juce::Justification::centredLeft);

        // a divider between the parts and the studio controls
        g.setColour (Colours::line);
        g.drawVerticalLine (dividerX, 14.0f, (float) getHeight() - 14.0f);
        g.drawVerticalLine (divider2X, 14.0f, (float) getHeight() - 14.0f);

        if (statusArea.getWidth() > 60)
        {
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.setColour (Colours::textDim);
            auto* dev = hub.deviceManager.getCurrentAudioDevice();
            juce::StringArray lines;
            if (dev == nullptr) lines.add ("NO INTERFACE - CLICK INTERFACE");
            else
            {
                lines.add (dev->getName().toUpperCase());
                lines.add (juce::String (dev->getCurrentSampleRate() / 1000.0, 1) + " kHz  \xc2\xb7  " + juce::String (dev->getCurrentBufferSizeSamples())
                           + "  \xc2\xb7  CPU " + juce::String (juce::roundToInt (hub.getLoad() * 100.0f)) + "%");
            }
            g.drawFittedText (lines.joinIntoString ("\n"), statusArea, juce::Justification::centredRight, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 6);
        r.removeFromLeft (wordmarkWidth - 14);
        for (auto* t : tiles) { t->setBounds (r.removeFromLeft (tileWidth)); r.removeFromLeft (gap); }
        r.removeFromLeft (6);
        dividerX = r.getX();
        r.removeFromLeft (14);
        transportStrip.setBounds (r.removeFromLeft (TransportStrip::preferredWidth));
        r.removeFromLeft (14);
        divider2X = r.getX();
        r.removeFromLeft (14);
        for (auto* t : { &sessionTile, &patchTile, &netTile, &audioTile, &updateTile }) { t->setBounds (r.removeFromLeft (tileWidth)); r.removeFromLeft (gap); }
        statusArea = r.withTrimmedLeft (10);
    }

    void showPatchbay()
    {
        if (patchWindow == nullptr)
        {
            patchWindow = std::make_unique<ToolWindow> ("MELVILLE \xc2\xb7 PATCHBAY");
            frameWindow (*patchWindow, new PatchbayView (hub), true, "Patchbay", Colours::accent, loadIcon ("melville", 64));
            patchWindow->setResizable (true, false);
            patchWindow->centreWithSize (patchWindow->getWidth(), patchWindow->getHeight());
        }
        patchWindow->setVisible (true);
        patchWindow->toFront (true);
    }

    void showAudio()
    {
        if (interfaceWindow == nullptr)
        {
            interfaceWindow = std::make_unique<ToolWindow> ("MELVILLE \xc2\xb7 INTERFACE");
            frameWindow (*interfaceWindow, new InterfacePanel (hub), true, "Interface", Colours::accent, loadIcon ("melville", 64),
                         WindowTitleBar::minimise | WindowTitleBar::close);
            interfaceWindow->setResizable (false, false);
            interfaceWindow->centreWithSize (interfaceWindow->getWidth(), interfaceWindow->getHeight());
        }
        interfaceWindow->setVisible (true);
        interfaceWindow->toFront (true);
    }

    void refreshUpdate()
    {
        const bool ready = updater.isUpdateAvailable() && ! updater.isDownloading();
        updateTile.setEnabled (ready);
        if (updater.isDownloading())
        {
            updateTile.setName (juce::String (juce::roundToInt (updater.getProgress() * 100.0f)) + "%");
            updateTile.setTooltip ("Downloading MELVILLE " + updater.getLatestVersion() + "...");
        }
        else
        {
            updateTile.setName ("Update");
            updateTile.setTooltip (ready ? "MELVILLE " + updater.getLatestVersion() + " is ready: click to install"
                                         : "MELVILLE " + Updater::currentVersion() + " is up to date");
        }
        updateTile.repaint();
    }

    void offerUpdate()
    {
        if (! updater.isUpdateAvailable()) return;
        auto notes = updater.getReleaseNotes();
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
            "Update MELVILLE to " + updater.getLatestVersion() + "?",
            (notes.isNotEmpty() ? "What's new:\n" + notes + "\n\n" : juce::String())
                + "MELVILLE closes, installs the update and starts again. Hawthorne finishes any take first.",
            "Install", "Not now", this,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (ok) updater.install(); }));
    }

    /** A tile per part, in order (IRVINGs come and go). */
    void rebuildTiles()
    {
        tiles.clear();
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto id = parts[i]->id;
            const bool irving = id.startsWith ("pequod");
            if (irving && id != "pequod") continue;      // one IRVING tile for every instance

            auto* t = tiles.add (new Tile (parts[i]->title, loadIcon (id), Colours::forPart (id)));
            if (irving)
            {
                // a dot per instance; click to bring one up, right-click for a new one
                t->lamps = [this]
                {
                    std::vector<bool> on;
                    for (auto& p : parts) if (p->id.startsWith ("pequod")) on.push_back (hub.isPartConnected (p->id));
                    return on;
                };
                t->setTooltip ("IRVING: click to bring it up. Right-click for a new instance");
                t->onClick = [this] { showIrvingMenu (false); };
                t->onRightClick = [this] { showIrvingMenu (true); };
            }
            else
            {
                t->isLit = [this, id] { return hub.isPartConnected (id); };
                t->onClick = [this, id]
                {
                    for (size_t k = 0; k < parts.size(); ++k)
                        if (parts[k]->id == id) windows[k]->show();
                };
            }
            addAndMakeVisible (t);
        }
        setSize (wordmarkWidth + (tiles.size() + 5) * (tileWidth + gap) + 26 + TransportStrip::preferredWidth + 28 + 200, barHeight);
        resized();
    }

    /** Click: with one IRVING, bring it up; with several, pick one. Right-click: new instance,
        show or remove one. */
    void showIrvingMenu (bool full)
    {
        std::vector<size_t> at;
        for (size_t k = 0; k < parts.size(); ++k) if (parts[k]->id.startsWith ("pequod")) at.push_back (k);
        if (! full && at.size() == 1) { windows[at.front()]->show(); return; }

        juce::PopupMenu m;
        if (full)
        {
            m.addItem (1, "New instance", onAddIrving != nullptr && canAddIrving && canAddIrving());
            m.addSeparator();
        }
        for (auto k : at)
            m.addItem (100 + (int) k, "Show " + parts[k]->title);
        if (full)
        {
            juce::PopupMenu remove;
            for (auto k : at)
                if (parts[k]->id != "pequod") remove.addItem (200 + (int) k, parts[k]->title);
            if (remove.getNumItems() > 0) { m.addSeparator(); m.addSubMenu ("Remove", remove); }
        }
        m.showMenuAsync ({}, [this] (int r)
        {
            if (r == 1 && onAddIrving) onAddIrving();
            else if (r >= 100 && r < 200 && (size_t) (r - 100) < windows.size()) windows[(size_t) (r - 100)]->show();
            else if (r >= 200 && (size_t) (r - 200) < parts.size() && onRemoveIrving) onRemoveIrving (parts[(size_t) (r - 200)]->id);
        });
    }

    std::function<void()> onAddIrving;
    std::function<bool()> canAddIrving;
    std::function<void (const juce::String&)> onRemoveIrving;

    void closePatchbay() { patchWindow = nullptr; }
    juce::Component* getPatchbayContent()
    {
        if (patchWindow == nullptr) return nullptr;
        auto* f = dynamic_cast<FramedContent*> (patchWindow->getContentComponent());
        return f != nullptr ? f->getContent() : patchWindow->getContentComponent();
    }
    juce::DocumentWindow* getPatchbayWindow() { return patchWindow.get(); }

    static constexpr int barHeight = 100, tileWidth = 84, gap = 4, wordmarkWidth = 160;

private:
    void timerCallback() override
    {
        repaint();
        if (++ticks % (4 * 60 * 30) == 0) updater.checkSoon();   // and every half hour
    }

    Hub& hub;
    Updater updater;
    int ticks = 0;
    std::vector<std::unique_ptr<PartUI>>& parts;
    std::vector<std::unique_ptr<PartWindow>>& windows;
    juce::OwnedArray<Tile> tiles;
    Tile patchTile { "Patchbay", {}, Colours::accent }, netTile { "MELVILLE Link", {}, Colours::accent }, audioTile { "Interface", {}, Colours::accent },
         updateTile { "Update", {}, Colours::accent }, sessionTile { "Session", {}, Colours::accent };
public:
    std::function<void()> onShowNetwork;
    std::function<void (juce::Component&)> onSessionMenu;
    void setSessionName (const juce::String& n)
    {
        sessionTile.setTooltip ("SESSION" + (n.isNotEmpty() ? " \"" + n + "\"" : juce::String())
                                + ": save or open the whole studio in one file (the reel, racks, mix, drums, IRVINGs and patch)");
    }
private:
    juce::Rectangle<int> statusArea;
    int dividerX = 0, divider2X = 0;
    TransportStrip transportStrip;
    std::unique_ptr<juce::DocumentWindow> patchWindow, interfaceWindow;
};

class BarWindow : public juce::DocumentWindow
{
public:
    explicit BarWindow (Bar* bar) : DocumentWindow ("MELVILLE", Colours::panel, DocumentWindow::minimiseButton | DocumentWindow::closeButton)
    {
        frameWindow (*this, bar, true, "MELVILLE", Colours::accent, loadIcon ("melville", 64),
                     WindowTitleBar::minimise | WindowTitleBar::close);
        setResizable (false, false);
        // at the top centre of the main screen
        auto area = juce::Rectangle<int> (0, 0, 1920, 1080);
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) area = d->userBounds.toNearestInt();
        setTopLeftPosition (area.getCentreX() - getWidth() / 2, area.getY() + 8);
        setVisible (true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

} // namespace melville

//==============================================================================
class MelvilleApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MELVILLE"; }
    const juce::String getApplicationVersion() override { return MELVILLE_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        using namespace melville;
        taskbar::setProcessIdentity();
        juce::PropertiesFile::Options o;
        o.applicationName = "MELVILLE";
        o.filenameSuffix = ".settings";
        o.folderName = "MELVILLE";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        // the hub first: the parts open their devices on it as they're made
        hub = std::make_unique<Hub>();
        auto audio = props.getUserSettings()->getXmlValue ("audio");
        hub->initialiseAudio (audio.get());
        if (auto patch = props.getUserSettings()->getXmlValue ("patch"))
            if (patch->getIntAttribute ("version") >= 3)      // (older patches were for Hawthorne's old jacks)
                hub->getPatchbay().fromXml (*patch);
        hub->extraLatency = props.getUserSettings()->getIntValue ("latencyExtra", 0);   // (INTERFACE > MEASURE)

        // Hawthorne Link, now MELVILLE's (the first time with the senders Hawthorne had)
        {
            juce::PropertiesFile::Options ho;
            ho.applicationName = "Hawthorne";
            ho.filenameSuffix = ".settings";
            ho.folderName = "Hawthorne";
            ho.osxLibrarySubFolder = "Application Support";
            juce::PropertiesFile hawthorneSettings (ho);
            hub->getNetwork().start (*props.getUserSettings(), &hawthorneSettings);
        }

        // every part, running from the start (windows open from the bar)
        parts.push_back (parts::makeHawthorne());
        parts.push_back (parts::makeEmerson());
        parts.push_back (parts::makeWhitman());
        parts.push_back (parts::makeThoreau());
        parts.push_back (parts::makePequod());
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);   // (Hawthorne sets its own as the default)

        // every part gets its own taskbar button and icon, in the same order every time:
        // Hawthorne up front, the rest down on the taskbar ready to click
        for (auto& p : parts)
        {
            windows.push_back (std::make_unique<PartWindow> (*p));
            taskbar::setWindowIdentity (*windows.back(), p->id, p->title, loadIcon (p->id, 256));
        }
        // the extra IRVINGs open last time
        const auto testIrvings = juce::SystemStats::getEnvironmentVariable ("MELVILLE_TEST_IRVINGS", "0").getIntValue();   // (tests)
        for (int n = 2; n <= juce::jlimit (1, Hub::maxIrvings, testIrvings > 0 ? testIrvings : props.getUserSettings()->getIntValue ("irvings", 1)); ++n)
            addIrving (n, false);

        auto* barContent = new Bar (*hub, parts, windows);
        barContent->onShowNetwork = [this] { showNetwork(); };
        barContent->onSessionMenu = [this] (juce::Component& c) { showSessionMenu (c); };
        barContent->setSessionName (sessionFile().getFileNameWithoutExtension());
        barContent->canAddIrving = [this] { return nextFreeIrving() > 0; };
        barContent->onAddIrving = [this] { if (auto n = nextFreeIrving()) { addIrving (n, true); refreshBar(); } };
        barContent->onRemoveIrving = [this] (const juce::String& id) { removeIrving (id); };
        bar = std::make_unique<BarWindow> (barContent);
        for (size_t i = 1; i < windows.size(); ++i)
        {
            windows[i]->show();
            windows[i]->setMinimised (true);
        }
        windows.front()->show();   // Hawthorne, as you'd start the day
        showFromCommandLine (commandLine);

        // Test hook (the workflow's smoke test): report which parts came up, then quit
        const auto quitAfter = juce::SystemStats::getEnvironmentVariable ("MELVILLE_QUIT_AFTER_MS", {}).getIntValue();
        if (quitAfter > 0)
            juce::Timer::callAfterDelay (quitAfter, [this]
            {
                // pictures of the bar and the patchbay, for checking the look
                const auto snaps = juce::SystemStats::getEnvironmentVariable ("MELVILLE_SNAPSHOT_DIR", {});
                if (snaps.isNotEmpty())
                {
                    auto save = [&] (juce::Component* c, const char* name)
                    {
                        if (c == nullptr) return;
                        juce::FileOutputStream out (juce::File (snaps).getChildFile (name));
                        out.setPosition (0); out.truncate();
                        juce::PNGImageFormat().writeImageToStream (c->createComponentSnapshot (c->getLocalBounds()), out);
                    };
                    auto* b = this->barContent();
                    save (b, "bar.png");
                    b->showPatchbay();
                    save (b->getPatchbayContent(), "patchbay.png");
                    if (auto* pv = b->getPatchbayContent())
                        for (auto* c : pv->getChildren())
                            if (auto* vp = dynamic_cast<juce::Viewport*> (c)) save (vp->getViewedComponent(), "patchbay-full.png");
                    save (bar.get(), "bar-window.png");
                    save (b->getPatchbayWindow(), "patchbay-window.png");
                    showNetwork();
                    save (networkWindow.get(), "link-window.png");
                    for (size_t i = 0; i < windows.size(); ++i)
                    {
                        windows[i]->show();
                        save (windows[i].get(), (parts[i]->id + ".png").toRawUTF8());
                    }
                }

                int up = 0;
                for (auto& p : parts)
                {
                    const bool on = hub->isPartConnected (p->id);
                    up += on ? 1 : 0;
                    std::printf ("MELVILLE smoke: %-10s %s\n", p->id.toRawUTF8(), on ? "running" : "NOT RUNNING");
                }
                // the session file round trip
                const auto sessionTest = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("melville-smoke.melville");
                bool sessionOk = saveSession (sessionTest) && openSession (sessionTest);
                if (auto x = juce::XmlDocument::parse (sessionTest))
                    std::printf ("MELVILLE smoke: session %s, %d parts in it\n", sessionOk ? "saved and reopened" : "FAILED",
                                 x->getNumChildElements() - 1);
                else sessionOk = false;
                std::printf ("MELVILLE smoke: %d of %d parts running, %d cords\n", up, (int) parts.size(),
                             (int) hub->getPatchbay().getAllCords().size());
                std::fflush (stdout);
                setApplicationReturnValue (up == (int) parts.size() && sessionOk ? 0 : 1);
                systemRequestedQuit();
            });
    }

    void shutdown() override
    {
        if (hub != nullptr)
        {
            props.getUserSettings()->setValue ("patch", hub->getPatchbay().toXml().get());
            props.getUserSettings()->setValue ("latencyExtra", hub->extraLatency.load());
            hub->getNetwork().saveSettings (*props.getUserSettings());
            if (auto xml = hub->deviceManager.createStateXml())
                props.getUserSettings()->setValue ("audio", xml.get());
        }
        for (auto& p : parts) if (p->shutdown) p->shutdown();   // Hawthorne finishes any take first

        // dialogs a part left open (IRVING's plug-in manager, say) go before the part does
        for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;)
            if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
            {
                dw->exitModalState (0);
                delete dw;
            }
        juce::ModalComponentManager::getInstance()->cancelAllModalComponents();

        networkWindow = nullptr;
        bar = nullptr;
        windows.clear();
        while (! parts.empty()) parts.pop_back();               // the parts close their devices
        hub = nullptr;
        props.closeFiles();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    //==========================================================================
    // More than one IRVING: each its own window, taskbar button and patch panel

    int nextFreeIrving() const
    {
        for (int n = 2; n <= melville::Hub::maxIrvings; ++n)
        {
            bool used = false;
            for (auto& p : parts) used = used || p->id == melville::Hub::irvingId (n);
            if (! used) return n;
        }
        return 0;
    }

    int countIrvings() const
    {
        int n = 0;
        for (auto& p : parts) n += p->id.startsWith ("pequod") ? 1 : 0;
        return n;
    }

    void addIrving (int n, bool show)
    {
        using namespace melville;
        auto ui = parts::makePequod (n);
        // after the other IRVINGs, so they sit together on the bar and the taskbar
        size_t at = 0;
        for (size_t k = 0; k < parts.size(); ++k) if (parts[k]->id.startsWith ("pequod")) at = k + 1;
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        auto window = std::make_unique<PartWindow> (*ui);
        taskbar::setWindowIdentity (*window, ui->id, ui->title, loadIcon (ui->id, 256));
        auto* w = window.get();
        parts.insert (parts.begin() + (long) at, std::move (ui));
        windows.insert (windows.begin() + (long) at, std::move (window));
        if (show) w->show();
        props.getUserSettings()->setValue ("irvings", countIrvings());
    }

    void removeIrving (const juce::String& id)
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove " + melville::Hub::irvingTitle (id.getTrailingIntValue()) + "?",
            "Its rack closes and its patch panel goes. Its cables come back if you open it again.", "Remove", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([this, id] (int ok)
            {
                if (! ok) return;
                for (size_t k = 0; k < parts.size(); ++k)
                    if (parts[k]->id == id)
                    {
                        windows.erase (windows.begin() + (long) k);       // the window first (it shows the part)
                        parts.erase (parts.begin() + (long) k);           // the part closes its device
                        break;
                    }
                hub->releasePart (id);
                props.getUserSettings()->setValue ("irvings", countIrvings());
                refreshBar();
            }));
    }

    //==========================================================================
    // The session: one file for the whole studio

    static constexpr const char* sessionExtension = ".melville";

    juce::File sessionFile()
    {
        const auto path = props.getUserSettings()->getValue ("session");
        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    static juce::File sessionsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MELVILLE").getChildFile ("Sessions");
    }

    void setSessionFile (const juce::File& f)
    {
        props.getUserSettings()->setValue ("session", f.getFullPathName());
        if (bar != nullptr)
            if (auto* b = barContent()) b->setSessionName (f.getFileNameWithoutExtension());
    }

    void showSessionMenu (juce::Component& target)
    {
        juce::PopupMenu m;
        const auto current = sessionFile();
        m.addItem ("Save session" + (current.existsAsFile() ? juce::String (" \"" + current.getFileNameWithoutExtension() + "\"") : juce::String()),
                   [this] { const auto f = sessionFile(); if (f.existsAsFile()) saveSession (f); else chooseSession (true); });
        m.addItem ("Save session as...", [this] { chooseSession (true); });
        m.addItem ("Open session...",    [this] { chooseSession (false); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    }

    void chooseSession (bool saving)
    {
        sessionsFolder().createDirectory();
        const auto start = sessionFile().existsAsFile() ? sessionFile() : sessionsFolder().getChildFile (juce::String ("Session") + sessionExtension);
        sessionChooser = std::make_unique<juce::FileChooser> (saving ? "Save the MELVILLE session" : "Open a MELVILLE session", start,
                                                              juce::String ("*") + sessionExtension);
        const int flags = saving ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                                 : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
        sessionChooser->launchAsync (flags, [this, saving] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            if (saving) saveSession (f.withFileExtension (sessionExtension));
            else        openSession (f);
        });
    }

    bool saveSession (const juce::File& f)
    {
        juce::XmlElement root ("MELVILLE_SESSION");
        root.setAttribute ("version", 1);
        root.setAttribute ("irvings", countIrvings());
        root.addChildElement (hub->getPatchbay().toXml().release());
        for (auto& [id, sp] : melville::sessionParts())
            if (sp.save)
                if (auto x = sp.save())
                {
                    auto* part = root.createNewChildElement ("PART");
                    part->setAttribute ("id", id);
                    part->addChildElement (x.release());
                }
        if (! root.writeTo (f))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MELVILLE", "Couldn't save " + f.getFullPathName());
            return false;
        }
        setSessionFile (f);
        return true;
    }

    bool openSession (const juce::File& f)
    {
        auto root = juce::XmlDocument::parse (f);
        if (root == nullptr || ! root->hasTagName ("MELVILLE_SESSION"))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MELVILLE", f.getFileName() + " isn't a MELVILLE session.");
            return false;
        }
        // the IRVINGs it had (their racks come back below)
        bool added = false;
        for (auto* part : root->getChildWithTagNameIterator ("PART"))
        {
            const auto id = part->getStringAttribute ("id");
            const int n = id.getTrailingIntValue();
            if (! id.startsWith ("pequod") || n < 2 || n > melville::Hub::maxIrvings) continue;
            bool have = false;
            for (auto& p : parts) have = have || p->id == id;
            if (! have) { addIrving (n, false); added = true; }
        }
        if (added) refreshBar();

        if (auto* patch = root->getChildByName ("PATCH"))
            hub->getPatchbay().fromXml (*patch);

        const auto& sessions = melville::sessionParts();
        for (auto* part : root->getChildWithTagNameIterator ("PART"))
        {
            auto it = sessions.find (part->getStringAttribute ("id"));
            if (it != sessions.end() && it->second.load && part->getFirstChildElement() != nullptr)
                it->second.load (*part->getFirstChildElement());
        }
        setSessionFile (f);
        return true;
    }

    void refreshBar()
    {
        if (bar != nullptr)
            if (auto* b = barContent())
            {
                b->rebuildTiles();
                bar->setContentComponentSize (b->getWidth(), b->getHeight());
            }
    }

    void showNetwork()
    {
        if (networkWindow == nullptr)
            networkWindow = melville::parts::makeNetworkWindow (hub->getNetwork().getReceiver(), [this]
            {
                hub->getNetwork().saveSettings (*props.getUserSettings());
                props.saveIfNeeded();
            });
        networkWindow->setVisible (true);
        networkWindow->toFront (true);
    }

    melville::Bar* barContent() const
    {
        if (bar == nullptr) return nullptr;
        auto* f = dynamic_cast<melville::FramedContent*> (bar->getContentComponent());
        return dynamic_cast<melville::Bar*> (f != nullptr ? f->getContent() : bar->getContentComponent());
    }

    void systemRequestedQuit() override { quit(); }
    // a pinned part's taskbar button runs MELVILLE.exe --show=<part>: bring that part up
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (! showFromCommandLine (commandLine) && bar != nullptr) bar->toFront (true);
    }

    bool showFromCommandLine (const juce::String& commandLine)
    {
        const auto id = commandLine.fromFirstOccurrenceOf ("--show=", false, true).upToFirstOccurrenceOf (" ", false, false).unquoted().trim();
        for (size_t i = 0; id.isNotEmpty() && i < parts.size(); ++i)
            if (parts[i]->id.equalsIgnoreCase (id)) { windows[i]->show(); return true; }
        return false;
    }

private:
    juce::ApplicationProperties props;
    std::unique_ptr<juce::FileChooser> sessionChooser;
    melville::MelvilleLookAndFeel lookAndFeel;
    std::unique_ptr<melville::Hub> hub;
    std::vector<std::unique_ptr<melville::PartUI>> parts;
    std::vector<std::unique_ptr<melville::PartWindow>> windows;
    std::unique_ptr<melville::BarWindow> bar;
    std::unique_ptr<juce::DocumentWindow> networkWindow;
};

START_JUCE_APPLICATION (MelvilleApplication)
