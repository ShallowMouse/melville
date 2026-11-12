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
#include "UI/ClockWindow.h"
#include "UI/TwainWindow.h"
#include "UI/PhonesServer.h"
#include "UI/OscControl.h"
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
    if (id == "twain")
    {
        // TWAIN's mark, drawn: a T whose bar is a pair of speakers and whose stem ends in headphones
        juce::Image img (juce::Image::ARGB, px, px, true);
        juce::Graphics g (img);
        const float s = (float) px;
        const auto orange = Colours::forPart ("twain");
        g.setColour (juce::Colour (0xff1c1d1f));
        g.fillRoundedRectangle (0.0f, 0.0f, s, s, s * 0.16f);
        g.setColour (orange);
        g.fillRoundedRectangle (s * 0.16f, s * 0.22f, s * 0.68f, s * 0.11f, s * 0.03f);        // the bar
        for (float cx : { 0.24f, 0.76f })
        {
            g.setColour (juce::Colour (0xff1c1d1f));
            g.fillEllipse (juce::Rectangle<float> (s * 0.26f, s * 0.26f).withCentre ({ s * cx, s * 0.275f }));
            g.setColour (orange);
            g.drawEllipse (juce::Rectangle<float> (s * 0.24f, s * 0.24f).withCentre ({ s * cx, s * 0.275f }), s * 0.035f);   // a speaker
            g.fillEllipse (juce::Rectangle<float> (s * 0.08f, s * 0.08f).withCentre ({ s * cx, s * 0.275f }));
        }
        g.fillRect (s * 0.455f, s * 0.33f, s * 0.09f, s * 0.36f);                                // the stem
        juce::Path band;                                                                         // headphones
        band.addCentredArc (s * 0.5f, s * 0.80f, s * 0.17f, s * 0.13f, 0.0f, -1.3f, 1.3f, true);
        g.strokePath (band, juce::PathStrokeType (s * 0.04f));
        for (float cx : { 0.33f, 0.67f })
            g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.09f, s * 0.14f).withCentre ({ s * cx, s * 0.84f }), s * 0.03f);
        return img;
    }
    if (id == "alcott")
    {
        // ALCOTT's mark, drawn: a keyboard (two octaves' worth of keys) under a music-stand arch
        juce::Image img (juce::Image::ARGB, px, px, true);
        juce::Graphics g (img);
        const float s = (float) px;
        const auto plum = Colours::forPart ("alcott");
        g.setColour (juce::Colour (0xff1c1d1f));
        g.fillRoundedRectangle (0.0f, 0.0f, s, s, s * 0.16f);
        g.setColour (plum);
        juce::Path arch;
        arch.addCentredArc (s * 0.5f, s * 0.46f, s * 0.30f, s * 0.22f, 0.0f, -1.45f, 1.45f, true);
        g.strokePath (arch, juce::PathStrokeType (s * 0.045f));
        const juce::Rectangle<float> kb (s * 0.16f, s * 0.46f, s * 0.68f, s * 0.34f);
        g.fillRoundedRectangle (kb, s * 0.03f);
        const int whites = 7;
        const float kw = kb.getWidth() / (float) whites;
        g.setColour (juce::Colour (0xff1c1d1f));
        for (int k = 1; k < whites; ++k)
            g.fillRect (kb.getX() + kw * (float) k - s * 0.006f, kb.getY(), s * 0.012f, kb.getHeight());
        for (int k : { 1, 2, 4, 5, 6 })
            g.fillRect (kb.getX() + kw * (float) k - kw * 0.3f, kb.getY(), kw * 0.6f, kb.getHeight() * 0.58f);
        return img;
    }
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
        : DocumentWindow (juce::String (juce::CharPointer_UTF8 ("MELVILLE \xc2\xb7 ")) + p.title, p.background, DocumentWindow::allButtons), part (p)
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
        keys[rtz]->onClick  = [] { melville::returnToZero(); };
        keys[stop]->onClick = [] { melville::stop(); };
        keys[play]->onClick = [] { melville::play(); };
        keys[rec]->onClick  = [] { melville::record(); };
        for (int k : { (int) rew, (int) ffw })
        {
            auto* b = keys[k];
            b->onStateChange = [b, k, held = false] () mutable
            {
                if (b->isDown() && ! held)       { held = true;  melville::wind (k == rew ? -1 : +1); }
                else if (! b->isDown() && held)  { held = false; melville::wind (0); }
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
        tempo.setTooltip ("MELVILLE's tempo (also kept on the reel when one is loaded): double-click to type one, right-click for beats per bar and bar 1. "
                          "THOREAU and IRVING follow it.");
        tempo.addMouseListener (this, false);
        tempo.onTextChange = [this]
        {
            const double b = tempo.getText().retainCharacters ("0123456789.").getDoubleValue();
            if (b >= 20.0 && b <= 400.0)
                {
                    auto& t = melville::transport();
                    melville::setTempo (b, t.beatsPerBar.load(), t.barOne.load());
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
        juce::String sub = t.available.load() && t.reelLoaded.load() ? juce::String ("TAPE") : juce::String ("CLOCK");
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
        juce::ignoreUnused (t);
        const bool have = true;      // (MELVILLE keeps the tempo with or without a reel)
        const double bpm = t.bpm.load() > 0.0 ? t.bpm.load() : 120.0;
        const int bpb = juce::jmax (1, t.beatsPerBar.load());
        auto set = [] (double b, int n, double one) { melville::setTempo (b, n, juce::jmax (0.0, one)); };

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
        const bool tape = t.available.load() && t.reelLoaded.load();
        for (auto* k : keys) k->setEnabled (tape || (k != keys[rec] && k != keys[rew] && k != keys[ffw]));
        refresh();
        repaint (counterArea);
    }

    juce::OwnedArray<Key> keys;
    juce::Label tempo;
    juce::Rectangle<int> counterArea, tempoCaption;
};

//==============================================================================
/** INTERFACE: the device selector, a round-trip latency measurement, and the control surface (OSC). */
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

        // the control surface (OSC)
        oscCaption.setText ("CONTROL SURFACE (OSC)", juce::dontSendNotification);
        oscCaption.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        oscCaption.setColour (juce::Label::textColourId, Colours::accent);
        addAndMakeVisible (oscCaption);
        oscHelp.setText ("The hardware (TWAIN, the Hawthorne transport) or TouchOSC. Point each at this computer's address and the "
                         "listen port; MELVILLE lights their keys back on the reply port, every surface at once.",
                         juce::dontSendNotification);
        oscHelp.setJustificationType (juce::Justification::topLeft);
        oscHelp.setColour (juce::Label::textColourId, Colours::textDim);
        addAndMakeVisible (oscHelp);
        oscOn.setButtonText ("On");
        oscOn.setTooltip ("Listen for a control surface");
        oscOn.onClick = [this] { applyOsc(); };
        addAndMakeVisible (oscOn);
        for (auto* e : { &listenPort, &replyPort })
        {
            e->setInputRestrictions (5, "0123456789");
            e->setJustification (juce::Justification::centred);
            e->onReturnKey = [this] { applyOsc(); };
            e->onFocusLost = [this] { applyOsc(); };
            addAndMakeVisible (e);
        }
        listenPort.setTooltip ("The port MELVILLE listens on (the surface's send port)");
        replyPort.setTooltip ("The port MELVILLE answers on (the surface's receive port)");
        replyTo.addItem ("every surface heard", 1);
        replyTo.addItem ("these addresses:", 2);
        replyTo.setTooltip ("Where MELVILLE sends the lamps and readouts: every surface it has heard from, or only these addresses (comma between them)");
        replyTo.onChange = [this] { replyHost.setEnabled (replyTo.getSelectedId() == 2); applyOsc(); };
        addAndMakeVisible (replyTo);
        replyHost.setTextToShowWhenEmpty ("192.168.1.20, 192.168.1.21", Colours::textDim);
        replyHost.setInputRestrictions (64);
        replyHost.onReturnKey = [this] { applyOsc(); };
        replyHost.onFocusLost = [this] { applyOsc(); };
        addAndMakeVisible (replyHost);
        for (auto* l : { &listenLabel, &replyLabel, &replyToLabel })
        {
            l->setColour (juce::Label::textColourId, Colours::text);
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        }
        listenLabel.setText ("Listen port", juce::dontSendNotification);
        replyLabel.setText ("Reply port", juce::dontSendNotification);
        replyToLabel.setText ("Reply to", juce::dontSendNotification);
        oscStatus.setColour (juce::Label::textColourId, Colours::text);
        oscStatus.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (oscStatus);
        showOsc();
        oscTimer.f = [this] { if (auto* o = melville::OscControl::instance()) oscStatus.setText (o->getStatus(), juce::dontSendNotification); };
        oscTimer.startTimer (500);

        setSize (580, 830);
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

        r.removeFromTop (14);
        oscTop = r.getY() - 7;
        oscCaption.setBounds (r.removeFromTop (22));
        oscHelp.setBounds (r.removeFromTop (38));
        r.removeFromTop (6);
        auto row1 = r.removeFromTop (26);
        oscOn.setBounds (row1.removeFromLeft (60)); row1.removeFromLeft (10);
        listenLabel.setBounds (row1.removeFromLeft (80)); row1.removeFromLeft (6);
        listenPort.setBounds (row1.removeFromLeft (64)); row1.removeFromLeft (16);
        replyLabel.setBounds (row1.removeFromLeft (76)); row1.removeFromLeft (6);
        replyPort.setBounds (row1.removeFromLeft (64));
        r.removeFromTop (8);
        auto row2 = r.removeFromTop (26);
        row2.removeFromLeft (70);
        replyToLabel.setBounds (row2.removeFromLeft (80)); row2.removeFromLeft (6);
        replyTo.setBounds (row2.removeFromLeft (170)); row2.removeFromLeft (8);
        replyHost.setBounds (row2.removeFromLeft (210));
        r.removeFromTop (8);
        oscStatus.setBounds (r.removeFromTop (56));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::bg);
        g.setColour (Colours::line);
        g.drawHorizontalLine (440, 12.0f, (float) getWidth() - 12.0f);
        g.drawHorizontalLine (oscTop, 12.0f, (float) getWidth() - 12.0f);
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

    void showOsc()
    {
        auto* o = melville::OscControl::instance();
        for (auto* c : std::initializer_list<juce::Component*> { &oscOn, &listenPort, &replyPort, &replyTo, &replyHost }) c->setEnabled (o != nullptr);
        if (o == nullptr) { oscStatus.setText ("Not running.", juce::dontSendNotification); return; }
        const auto& s = o->getSettings();
        oscOn.setToggleState (s.enabled, juce::dontSendNotification);
        listenPort.setText (juce::String (s.listenPort), false);
        replyPort.setText (juce::String (s.replyPort), false);
        replyTo.setSelectedId (s.replyHost.isEmpty() ? 1 : 2, juce::dontSendNotification);
        replyHost.setText (s.replyHost, false);
        replyHost.setEnabled (s.replyHost.isNotEmpty());
        oscStatus.setText (o->getStatus(), juce::dontSendNotification);
    }

    void applyOsc()
    {
        auto* o = melville::OscControl::instance();
        if (o == nullptr) return;
        melville::OscControl::Settings s;
        s.enabled = oscOn.getToggleState();
        s.listenPort = juce::jlimit (1, 65535, listenPort.getText().getIntValue() > 0 ? listenPort.getText().getIntValue() : 8100);
        s.replyPort = juce::jlimit (1, 65535, replyPort.getText().getIntValue() > 0 ? replyPort.getText().getIntValue() : 9100);
        s.replyHost = replyTo.getSelectedId() == 2 ? replyHost.getText().trim() : juce::String();
        const auto& now = o->getSettings();
        if (s.enabled == now.enabled && s.listenPort == now.listenPort && s.replyPort == now.replyPort && s.replyHost == now.replyHost)
            return;
        o->apply (s);
        showOsc();
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

    juce::Label oscCaption, oscHelp, oscStatus, listenLabel, replyLabel, replyToLabel;
    juce::ToggleButton oscOn;
    juce::TextEditor listenPort, replyPort, replyHost;
    juce::ComboBox replyTo;
    int oscTop = 0;
    struct Every : juce::Timer { std::function<void()> f; void timerCallback() override { if (f) f(); } } oscTimer;
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
        sessionTile.onClick = [this] { if (onShowLibrary) onShowLibrary(); };
        sessionTile.onRightClick = [this] { if (onSessionMenu) onSessionMenu (sessionTile); };
        sessionTile.setTooltip ("LIBRARY: your sessions and reels. Each reel keeps the whole studio with it (racks, mix, drums, IRVINGs, patch, tempo) "
                                "and its session sheet. Right-click: session sheet, save, export");
        addAndMakeVisible (sessionTile);

        sheetTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // a clipboard: the session sheet (notes, track sheet, take log, photos)
            auto box = r.reduced (r.getWidth() * 0.2f, r.getHeight() * 0.1f);
            g.setColour (Colours::panelHi);
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (Colours::textDim);
            g.drawRoundedRectangle (box, 3.0f, 1.5f);
            g.setColour (Colours::accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (box.getWidth() * 0.44f, box.getHeight() * 0.12f)
                                        .withCentre ({ box.getCentreX(), box.getY() + 1.0f }), 2.0f);
            for (int k = 0; k < 4; ++k)
                g.fillRect (box.getX() + box.getWidth() * 0.18f, box.getY() + box.getHeight() * (0.3f + 0.16f * (float) k),
                            box.getWidth() * (k == 3 ? 0.4f : 0.64f), 2.0f);
        };
        sheetTile.onClick = []
        {
            auto& l = melville::library();
            const bool onDeck = l.currentReel && l.currentReel().existsAsFile();
            if (onDeck) { if (l.showSheet) l.showSheet(); }
            else if (l.show) l.show();       // (no reel on the deck: the Library, to pick one)
        };
        sheetTile.setTooltip ("SESSION SHEET: the reel on the deck's notes, track sheet, take log and photos");
        addAndMakeVisible (sheetTile);

        netTile.onClick = [this] { if (onShowNetwork) onShowNetwork(); };

        clockTile.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            // a metronome: MELVILLE's tempo, click and timecode
            auto b = r.reduced (r.getWidth() * 0.16f);
            juce::Path p;
            p.startNewSubPath (b.getX() + b.getWidth() * 0.36f, b.getY());
            p.lineTo (b.getX() + b.getWidth() * 0.64f, b.getY());
            p.lineTo (b.getRight() - b.getWidth() * 0.06f, b.getBottom());
            p.lineTo (b.getX() + b.getWidth() * 0.06f, b.getBottom());
            p.closeSubPath();
            g.setColour (Colours::textDim);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (Colours::accent);
            g.drawLine (b.getCentreX(), b.getBottom() - b.getHeight() * 0.2f, b.getRight() - b.getWidth() * 0.18f, b.getY() + b.getHeight() * 0.12f, 2.5f);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ b.getCentreX() + b.getWidth() * 0.16f, b.getY() + b.getHeight() * 0.42f }));
        };
        clockTile.onClick = [this] { if (onShowClock) onShowClock(); };
        clockTile.setTooltip ("CLOCK: MELVILLE's tempo, click and timecode (LTC / MTC out, chase)");
        addAndMakeVisible (clockTile);
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
        setSize (wordmarkWidth + (tiles.size() + 7) * (tileWidth + gap) + 26 + TransportStrip::preferredWidth + 28 + 200, barHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::panel);
        auto r = getLocalBounds().reduced (14, 0);
        g.setColour (Colours::accent);
        g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));     // (MELVILLE's own: plain bold sans, unlike the parts)
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
                lines.add (juce::String (dev->getCurrentSampleRate() / 1000.0, 1) + juce::String (juce::CharPointer_UTF8 (" kHz  \xc2\xb7  ")) + juce::String (dev->getCurrentBufferSizeSamples())
                           + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  CPU ")) + juce::String (juce::roundToInt (hub.getLoad() * 100.0f)) + "%");
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
        for (auto* t : { &sessionTile, &sheetTile, &clockTile, &patchTile, &netTile, &audioTile, &updateTile }) { t->setBounds (r.removeFromLeft (tileWidth)); r.removeFromLeft (gap); }
        statusArea = r.withTrimmedLeft (10);
    }

    void showPatchbay()
    {
        if (patchWindow == nullptr)
        {
            patchWindow = std::make_unique<ToolWindow> (juce::String (juce::CharPointer_UTF8 ("MELVILLE \xc2\xb7 PATCHBAY")));
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
            interfaceWindow = std::make_unique<ToolWindow> (juce::String (juce::CharPointer_UTF8 ("MELVILLE \xc2\xb7 INTERFACE")));
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
                t->isLit = [this, id] { return id == "twain" || hub.isPartConnected (id); };    // (TWAIN is MELVILLE's own: always on)
                if (id == "twain") t->setTooltip ("TWAIN: the control room. What you listen to, on which speakers, and the players' headphone mixes");
                t->onClick = [this, id]
                {
                    for (size_t k = 0; k < parts.size(); ++k)
                        if (parts[k]->id == id) windows[k]->show();
                };
            }
            addAndMakeVisible (t);
        }
        setSize (wordmarkWidth + (tiles.size() + 7) * (tileWidth + gap) + 26 + TransportStrip::preferredWidth + 28 + 200, barHeight);
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
    juce::DocumentWindow* getInterfaceWindow() { showAudio(); return interfaceWindow.get(); }

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
         updateTile { "Update", {}, Colours::accent }, sessionTile { "Library", {}, Colours::accent }, sheetTile { "Sheet", {}, Colours::accent }, clockTile { "Clock", {}, Colours::accent };
public:
    std::function<void()> onShowNetwork, onShowClock, onShowTwain;
    std::function<void (juce::Component&)> onSessionMenu;
    std::function<void()> onShowLibrary;
    void setSessionName (const juce::String& n)
    {
        sessionTile.setTooltip ("LIBRARY" + (n.isNotEmpty() ? " - on the deck: " + n : juce::String())
                                + ". Each reel keeps the whole studio with it. Right-click: session sheet, save, export");
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
        // (smoke runs with pictures: the new-session wizard's steps go in the same folder;
        //  WHITMAN_PREVIEW_DESK=<number> with WHITMAN_PREVIEW_STRIPS pictures one of WHITMAN's desks,
        //  HAWTHORNE_ONLY=27 in Hawthorne's engine tests checks the tapes against their datasheets)
        if (const auto snaps = juce::SystemStats::getEnvironmentVariable ("MELVILLE_SNAPSHOT_DIR", {}); snaps.isNotEmpty())
        {
           #if JUCE_WINDOWS
            _putenv_s ("HAWTHORNE_WIZARD_GALLERY", snaps.toRawUTF8());
           #else
            setenv ("HAWTHORNE_WIZARD_GALLERY", snaps.toRawUTF8(), 1);
           #endif
        }
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
        hub->onBlockStart = [] (juce::int64 frame, int n, double sr) { melville::clockBlock (frame, n, sr); };
        hub->getClock().getState = [&clock = hub->getClock()]
        {
            // MELVILLE's clock at the start of this block (the tape, or MELVILLE's own)
            auto& t = melville::transport();
            melville::ClockNode::State s;
            int flags = 0;
            s.valid = melville::tapePositionAt (melville::currentFrame(), s.pos, s.rate, flags);
            s.rolling = (flags & 1) != 0;
            s.clockRate = t.reelRate.load (std::memory_order_relaxed);
            s.bpm = t.bpm.load (std::memory_order_relaxed);
            s.beatsPerBar = t.beatsPerBar.load (std::memory_order_relaxed);
            s.barOne = t.barOne.load (std::memory_order_relaxed);
            clock.tcRate.store (t.tcRate.load (std::memory_order_relaxed), std::memory_order_relaxed);
            clock.tcStart.store (t.tcStart.load (std::memory_order_relaxed), std::memory_order_relaxed);
            clock.recording.store ((flags & 2) != 0, std::memory_order_relaxed);
            return s;
        };
        loadClockSettings();
        if (auto tw = props.getUserSettings()->getXmlValue ("twain")) melville::twainFromXml (hub->getTwain(), *tw);
        // the players set their own headphone mixes on their phones
        phones = std::make_unique<melville::PhonesServer> (hub->getTwain());
        phones->start();
        // a control surface (TouchOSC now, the hardware TWAIN later) over OSC
        osc = std::make_unique<melville::OscControl> (hub->getTwain());
        {
            auto saved = props.getUserSettings()->getXmlValue ("oscControl");
            osc->apply (melville::OscControl::Settings::fromXml (saved.get()));
            osc->onSettingsChanged = [this] (const melville::OscControl::Settings& s)
            {
                props.getUserSettings()->setValue ("oscControl", s.toXml().get());
                props.saveIfNeeded();
            };
        }
        phonesNames.f = [this] { if (phones != nullptr && hub != nullptr) phones->setCueNames (melville::twainCueNames (*hub)); };
        phonesNames.startTimer (1000);
        mtcOut = std::make_unique<melville::MtcOut> (hub->getClock());
        {
            // MELVILLE's tempo (a reel with its own takes over while it's loaded)
            auto* s = props.getUserSettings();
            auto& t = melville::transport();
            t.bpm = s->getDoubleValue ("tempoBpm", 120.0);
            t.beatsPerBar = s->getIntValue ("tempoBeatsPerBar", 4);
        }
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
        {
            // TWAIN, the control room: MELVILLE's own part, first on the bar and the taskbar
            auto tw = std::make_unique<PartUI>();
            tw->id = "twain";
            tw->title = "TWAIN";
            tw->background = juce::Colour (0xff121314);
            tw->minWidth = 900;
            tw->minHeight = 560;
            auto tp = std::make_unique<melville::TwainPanel> (*hub);
            tp->phonesUrl = [this] { return phones != nullptr ? phones->getUrl() : juce::String(); };
            tp->oscStatus = [this] { return osc == nullptr ? juce::String() : osc->getStatus() + " (set up in INTERFACE)"; };
            tw->content = std::move (tp);
            parts.push_back (std::move (tw));
        }
        parts.push_back (parts::makeHawthorne());
        parts.push_back (parts::makeEmerson());
        parts.push_back (parts::makeWhitman());
        parts.push_back (parts::makeThoreau());
        parts.push_back (parts::makePequod());
        parts.push_back (parts::makeAlcott());
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
        barContent->onShowClock = [this] { showClock(); };
        barContent->onShowTwain = [this] { showTwain(); };
        barContent->onSessionMenu = [this] (juce::Component& c) { showLibraryMenu (c); };
        barContent->onShowLibrary = [this] { showLibrary(); };
        {
            auto& l = melville::library();
            l.show = [this] { showLibrary(); };
            l.showDeck = [this] { showDeck(); };
            l.reelLeaving = [this] (const juce::File& f) { saveStudioWith (f); };
            l.reelArrived = [this] (const juce::File& f) { studioArrived (f); };
        }
        barContent->setSessionName (sessionFile().getFileNameWithoutExtension());
        barContent->canAddIrving = [this] { return nextFreeIrving() > 0; };
        barContent->onAddIrving = [this] { if (auto n = nextFreeIrving()) { addIrving (n, true); refreshBar(); } };
        barContent->onRemoveIrving = [this] (const juce::String& id) { removeIrving (id); };
        bar = std::make_unique<BarWindow> (barContent);
        for (size_t i = 0; i < windows.size(); ++i)
        {
            if (parts[i]->id == "hawthorne") continue;
            windows[i]->show();
            windows[i]->setMinimised (true);
        }
        showPart ("hawthorne");    // Hawthorne, as you'd start the day
        applyClockSettings();      // (MTC out, and Hawthorne's chase)
        {
            // no reel on the deck yet: the library is the first thing you see (as it was in Hawthorne)
            auto& l = melville::library();
            if (l.currentReel && ! l.currentReel().existsAsFile()) showLibrary();
        }
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
                            if (auto* vp = dynamic_cast<juce::Viewport*> (c))
                            {
                                save (vp->getViewedComponent(), "patchbay-full.png");
                                // and the MIDI tab
                                for (auto* t : pv->getChildren())
                                    if (auto* tb = dynamic_cast<juce::TextButton*> (t); tb != nullptr && tb->getButtonText() == "MIDI")
                                    {
                                        tb->triggerClick();
                                        juce::Timer::callAfterDelay (300, [snaps, sp = juce::Component::SafePointer<juce::Component> (vp->getViewedComponent())]
                                        {
                                            if (sp == nullptr) return;
                                            juce::FileOutputStream out (juce::File (snaps).getChildFile ("patchbay-midi.png"));
                                            out.setPosition (0); out.truncate();
                                            juce::PNGImageFormat().writeImageToStream (sp->createComponentSnapshot (sp->getLocalBounds()), out);
                                        });
                                    }
                            }
                    save (bar.get(), "bar-window.png");
                    save (b->getPatchbayWindow(), "patchbay-window.png");
                    save (b->getInterfaceWindow(), "interface-window.png");
                    showNetwork();
                    save (networkWindow.get(), "link-window.png");
                    showClock();
                    save (clockWindow.get(), "clock-window.png");
                    showLibrary();
                    save (libraryWindow.get(), "library-window.png");
                    for (size_t i = 0; i < windows.size(); ++i)
                    {
                        windows[i]->show();
                        save (windows[i].get(), (parts[i]->id + ".png").toRawUTF8());
                    }
                    // every part's settings window (its settings key, clicked)
                    std::function<juce::Button* (juce::Component&)> findSettings = [&] (juce::Component& c) -> juce::Button*
                    {
                        if (auto* b = dynamic_cast<juce::Button*> (&c))
                            if (b->isShowing() && (b->getTooltip().startsWithIgnoreCase ("Settings") || b->getName().equalsIgnoreCase ("Settings"))) return b;
                        for (auto* ch : c.getChildren()) if (auto* b = findSettings (*ch)) return b;
                        return nullptr;
                    };
                    for (size_t i = 0; i < windows.size(); ++i)
                    {
                        windows[i]->show();
                        auto* b = findSettings (*windows[i]);
                        std::printf ("MELVILLE smoke: %-10s settings key %s\n", parts[i]->id.toRawUTF8(), b != nullptr ? "yes" : "NONE");
                        if (b != nullptr) b->triggerClick();
                    }
                    juce::Timer::callAfterDelay (700, [snaps]
                    {
                        for (int w = 0; w < juce::TopLevelWindow::getNumTopLevelWindows(); ++w)
                            if (auto* tw = juce::TopLevelWindow::getTopLevelWindow (w))
                                if (tw->isVisible() && tw->getName().containsIgnoreCase ("settings"))
                                {
                                    juce::FileOutputStream out (juce::File (snaps).getChildFile (tw->getName().replaceCharacter (' ', '-') + ".png"));
                                    out.setPosition (0); out.truncate();
                                    juce::PNGImageFormat().writeImageToStream (tw->createComponentSnapshot (tw->getLocalBounds()), out);
                                }
                    });
                }

                int up = 0;
                for (auto& p : parts)
                {
                    const bool on = p->id == "twain" || hub->isPartConnected (p->id);
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
                // (with pictures to take: a moment for the settings windows first)
                const bool pictures = juce::SystemStats::getEnvironmentVariable ("MELVILLE_SNAPSHOT_DIR", {}).isNotEmpty();
                juce::Timer::callAfterDelay (pictures ? 1500 : 1, [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
            });
    }

    void shutdown() override
    {
        if (hub != nullptr)
        {
            props.getUserSettings()->setValue ("patch", hub->getPatchbay().toXml().get());
            props.getUserSettings()->setValue ("latencyExtra", hub->extraLatency.load());
            props.getUserSettings()->setValue ("tempoBpm", melville::transport().bpm.load());
            saveClockSettings();
            props.getUserSettings()->setValue ("twain", melville::twainToXml (hub->getTwain()).get());
            props.getUserSettings()->setValue ("tempoBeatsPerBar", melville::transport().beatsPerBar.load());
            hub->getNetwork().saveSettings (*props.getUserSettings());
            if (auto xml = hub->deviceManager.createStateXml())
                props.getUserSettings()->setValue ("audio", xml.get());
        }
        // the studio goes back with the reel on the deck
        if (auto& f = melville::library().currentReel) saveStudioWith (f());
        for (auto& p : parts) if (p->shutdown) p->shutdown();   // Hawthorne finishes any take first

        // dialogs a part left open (IRVING's plug-in manager, say) go before the part does
        for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;)
            if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
            {
                dw->exitModalState (0);
                delete dw;
            }
        juce::ModalComponentManager::getInstance()->cancelAllModalComponents();
        melville::closePanels();      // (the parts' settings windows, before the parts)

        networkWindow = nullptr;
        clockWindow = nullptr;
        phonesNames.stopTimer();
        osc = nullptr;
        phones = nullptr;
        if (libraryWindow != nullptr)
        {
            if (auto& v = melville::library().view) if (auto* c = v()) c->setLookAndFeel (nullptr);
            libraryWindow->clearContentComponent(); libraryWindow = nullptr;   // (the view is Hawthorne's)
        }
        {
            auto& l = melville::library();
            l.show = l.showDeck = nullptr;
            l.reelLeaving = l.reelArrived = nullptr;
        }
        mtcOut = nullptr;
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

    /** The whole studio as XML: tempo and timecode, the patch, and every part's state. */
    std::unique_ptr<juce::XmlElement> studioXml()
    {
        auto root = std::make_unique<juce::XmlElement> ("MELVILLE_SESSION");
        root->setAttribute ("version", 1);
        root->setAttribute ("irvings", countIrvings());
        root->addChildElement (hub->getPatchbay().toXml().release());
        {
            auto& t = melville::transport();
            auto& c = hub->getClock();
            auto* tempo = root->createNewChildElement ("TEMPO");
            tempo->setAttribute ("bpm", t.bpm.load());
            tempo->setAttribute ("beatsPerBar", t.beatsPerBar.load());
            tempo->setAttribute ("barOne", t.barOne.load());
            tempo->setAttribute ("tcRate", t.tcRate.load());
            tempo->setAttribute ("tcStart", t.tcStart.load());
            tempo->setAttribute ("clickOn", c.clickOn.load());
            tempo->setAttribute ("clickGain", (double) c.clickGain.load());
        }
        root->addChildElement (melville::twainToXml (hub->getTwain()).release());
        for (auto& [id, sp] : melville::sessionParts())
            if (sp.save)
                if (auto x = sp.save())
                {
                    auto* part = root->createNewChildElement ("PART");
                    part->setAttribute ("id", id);
                    part->addChildElement (x.release());
                }
        return root;
    }

    /** Puts the studio back as it was saved. withReel: also put that reel on Hawthorne's deck. */
    void applyStudio (const juce::XmlElement& root, bool withReel)
    {
        // the IRVINGs it had (their racks come back below)
        bool added = false;
        for (auto* part : root.getChildWithTagNameIterator ("PART"))
        {
            const auto id = part->getStringAttribute ("id");
            const int n = id.getTrailingIntValue();
            if (! id.startsWith ("pequod") || n < 2 || n > melville::Hub::maxIrvings) continue;
            bool have = false;
            for (auto& p : parts) have = have || p->id == id;
            if (! have) { addIrving (n, false); added = true; }
        }
        if (added) refreshBar();

        if (auto* patch = root.getChildByName ("PATCH"))
            hub->getPatchbay().fromXml (*patch);
        if (auto* tw = root.getChildByName ("TWAIN"))
        {
            melville::twainFromXml (hub->getTwain(), *tw);
            for (auto& p : parts)
                if (auto* tp = dynamic_cast<melville::TwainPanel*> (p->content.get())) tp->refresh();
        }

        const auto& sessions = melville::sessionParts();
        for (auto* part : root.getChildWithTagNameIterator ("PART"))
        {
            const auto id = part->getStringAttribute ("id");
            if (id == "hawthorne" && ! withReel) continue;
            auto it = sessions.find (id);
            if (it != sessions.end() && it->second.load && part->getFirstChildElement() != nullptr)
                it->second.load (*part->getFirstChildElement());
        }
        if (auto* tempo = root.getChildByName ("TEMPO"))
        {
            melville::setTempo (tempo->getDoubleAttribute ("bpm", 120.0), tempo->getIntAttribute ("beatsPerBar", 4),
                                tempo->getDoubleAttribute ("barOne", 0.0));
            melville::setTimecode (tempo->getIntAttribute ("tcRate", melville::transport().tcRate.load()),
                                   tempo->getDoubleAttribute ("tcStart", melville::transport().tcStart.load()));
            auto& c = hub->getClock();
            c.clickOn = tempo->getBoolAttribute ("clickOn", c.clickOn.load());
            c.clickGain = (float) tempo->getDoubleAttribute ("clickGain", c.clickGain.load());
        }
    }

    bool saveSession (const juce::File& f)
    {
        if (! studioXml()->writeTo (f))
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
        applyStudio (*root, true);
        setSessionFile (f);
        return true;
    }

    //==========================================================================
    // The library: each reel keeps the whole studio with it

    static constexpr const char* studioFileName = "MELVILLE.studio";

    void saveStudioWith (const juce::File& reelFile)
    {
        if (! reelFile.existsAsFile()) return;
        auto x = studioXml();
        x->setAttribute ("reel", reelFile.getFileName());
        x->writeTo (reelFile.getParentDirectory().getChildFile (studioFileName));
    }

    void studioArrived (const juce::File& reelFile)
    {
        // a reel that has a studio saved with it brings it back; a new one keeps the studio as it is
        if (auto x = juce::XmlDocument::parse (reelFile.getParentDirectory().getChildFile (studioFileName)))
            if (x->hasTagName ("MELVILLE_SESSION"))
                applyStudio (*x, false);
        if (bar != nullptr)
            if (auto* b = barContent()) b->setSessionName (reelFile.getParentDirectory().getParentDirectory().getFileName()
                                                            + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + reelFile.getFileNameWithoutExtension());
    }

    void showLibrary()
    {
        auto& l = melville::library();
        if (! l.view) return;
        if (libraryWindow == nullptr)
        {
            libraryWindow = std::make_unique<melville::ToolWindow> (juce::String (juce::CharPointer_UTF8 ("MELVILLE \xc2\xb7 LIBRARY")));
            auto* view = l.view();
            // MELVILLE's look, not Hawthorne's
            view->setLookAndFeel (&lookAndFeel);
            if (l.style)
                l.style (melville::Colours::accent, juce::Colour (0xff10201e), melville::Colours::bg, melville::Colours::panel,
                         melville::Colours::text, melville::Colours::textDim, melville::loadIcon ("melville", 128));
            view->setSize (1300, 820);
            view->setVisible (true);
            melville::frameWindow (*libraryWindow, view, false, "Library", melville::Colours::accent, melville::loadIcon ("melville", 64));
            libraryWindow->setResizable (true, false);
            libraryWindow->setResizeLimits (900, 560, 6000, 4000);
            libraryWindow->centreWithSize (libraryWindow->getWidth(), libraryWindow->getHeight());
        }
        if (l.refresh) l.refresh();
        libraryWindow->setVisible (true);
        libraryWindow->toFront (true);
    }

    void showDeck()
    {
        if (libraryWindow != nullptr) libraryWindow->setVisible (false);
        showPart ("hawthorne");
    }

    void showLibraryMenu (juce::Component& target)
    {
        juce::PopupMenu m;
        auto& l = melville::library();
        const auto reel = l.currentReel ? l.currentReel() : juce::File();
        m.addItem ("Open the library", [this] { showLibrary(); });
        m.addItem ("Session sheet (notes, track sheet, take log, photos)", reel.existsAsFile(), false, [] { if (auto& f = melville::library().showSheet) f(); });
        m.addSeparator();
        m.addItem ("Save the studio with \"" + (reel.existsAsFile() ? reel.getFileNameWithoutExtension() : juce::String ("the reel")) + "\"",
                   reel.existsAsFile(), false, [this, reel] { saveStudioWith (reel); });
        m.addSeparator();
        m.addItem ("Export the studio to a file...", [this] { chooseSession (true); });
        m.addItem ("Import a studio file...",       [this] { chooseSession (false); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
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
    std::unique_ptr<juce::DocumentWindow> networkWindow, clockWindow, libraryWindow;
    std::unique_ptr<melville::PhonesServer> phones;
    std::unique_ptr<melville::OscControl> osc;
    struct Every : juce::Timer { std::function<void()> f; void timerCallback() override { f(); } } phonesNames;

    void showTwain() { showPart ("twain"); }

    /** A part's window, up front. */
    void showPart (const juce::String& id)
    {
        for (size_t i = 0; i < parts.size(); ++i)
            if (parts[i]->id == id) { windows[i]->show(); return; }
    }

    std::unique_ptr<melville::MtcOut> mtcOut;
    melville::ClockPanel::Settings clockSettings;

    void loadClockSettings()
    {
        auto* s = props.getUserSettings();
        auto& c = hub->getClock();
        auto& t = melville::transport();
        c.clickOn = s->getBoolValue ("clickOn", false);
        c.clickOnlyRecording = s->getBoolValue ("clickOnlyRecording", false);
        c.accentGain = s->getBoolValue ("clickAccent", true) ? 1.0f : 0.7f;
        c.clickGain = (float) s->getDoubleValue ("clickGain", 0.25);
        c.ltcOn = s->getBoolValue ("ltcOn", true);
        c.ltcGain = (float) s->getDoubleValue ("ltcGain", 0.3);
        t.tcRate = s->getIntValue ("tcRate", 1);
        t.tcStart = s->getDoubleValue ("tcStart", 0.0);
        clockSettings.mtcOut = s->getValue ("mtcOut");
        clockSettings.mtcIn = s->getValue ("mtcIn");
        clockSettings.chase = s->getIntValue ("chase", 0);
    }

    void saveClockSettings()
    {
        auto* s = props.getUserSettings();
        auto& c = hub->getClock();
        auto& t = melville::transport();
        s->setValue ("clickOn", c.clickOn.load());
        s->setValue ("clickOnlyRecording", c.clickOnlyRecording.load());
        s->setValue ("clickAccent", c.accentGain.load() > 0.85f);
        s->setValue ("clickGain", (double) c.clickGain.load());
        s->setValue ("ltcOn", c.ltcOn.load());
        s->setValue ("ltcGain", (double) c.ltcGain.load());
        s->setValue ("tcRate", t.tcRate.load());
        s->setValue ("tcStart", t.tcStart.load());
        s->setValue ("mtcOut", clockSettings.mtcOut);
        s->setValue ("mtcIn", clockSettings.mtcIn);
        s->setValue ("chase", clockSettings.chase);
    }

    void applyClockSettings()
    {
        if (mtcOut != nullptr && ! mtcOut->open (clockSettings.mtcOut))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MELVILLE", "Couldn't open that MIDI output for MTC.");
        if (auto& f = melville::transport().setChase) f (clockSettings.chase, clockSettings.mtcIn);
    }

    void showClock()
    {
        if (clockWindow == nullptr)
        {
            clockWindow = std::make_unique<melville::ToolWindow> (juce::String (juce::CharPointer_UTF8 ("MELVILLE \xc2\xb7 CLOCK")));
            auto* panel = new melville::ClockPanel (hub->getClock(), clockSettings);
            panel->onSettingsChanged = [this] (const melville::ClockPanel::Settings& s)
            {
                clockSettings = s;
                applyClockSettings();
                saveClockSettings();
                props.saveIfNeeded();
            };
            melville::frameWindow (*clockWindow, panel, true, "Clock", melville::Colours::accent, melville::loadIcon ("melville", 64),
                                   melville::WindowTitleBar::minimise | melville::WindowTitleBar::close);
            clockWindow->setResizable (false, false);
            clockWindow->centreWithSize (clockWindow->getWidth(), clockWindow->getHeight());
        }
        clockWindow->setVisible (true);
        clockWindow->toFront (true);
    }
};

START_JUCE_APPLICATION (MelvilleApplication)
