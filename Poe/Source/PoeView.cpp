#include "PoeView.h"
#include <MelvilleHosted.h>

namespace poe
{

namespace
{
    const juce::Colour ink { 0xffe9e4d6 };              // the panel's legends: white paint
    const juce::Colour inkDim { 0xff8a857b };
    const juce::Colour face { 0xff222225 };             // black-anodised panel
    const juce::Colour lampRed { 0xffff4a2e };          // the step lamps: incandescent behind red glass

    constexpr float arcStart = -2.6f, arcEnd = 2.6f;    // the knobs' travel (radians from twelve o'clock)

    /** A walnut end cheek. */
    void cheek (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a3520), r.getX(), 0.0f, juce::Colour (0xff3a2112), r.getRight(), 0.0f, false));
        g.fillRoundedRectangle (r, 3.0f);
        juce::Random rnd (1966);
        for (int i = 0; i < 26; ++i)
        {
            const float x = r.getX() + rnd.nextFloat() * r.getWidth();
            g.setColour (juce::Colour (0xff24140a).withAlpha (0.12f + 0.25f * rnd.nextFloat()));
            juce::Path grain;
            grain.startNewSubPath (x, r.getY());
            grain.cubicTo (x + 2.0f * rnd.nextFloat() - 1.0f, r.getY() + r.getHeight() * 0.33f,
                           x + 2.0f * rnd.nextFloat() - 1.0f, r.getY() + r.getHeight() * 0.66f, x, r.getBottom());
            g.strokePath (grain, juce::PathStrokeType (0.6f + rnd.nextFloat()));
        }
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

    void facePanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        Pequod::hw::panel (g, r, face, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        for (auto p : { r.getTopLeft() + juce::Point<float> (9.0f, 9.0f), r.getTopRight() + juce::Point<float> (-9.0f, 9.0f),
                        r.getBottomLeft() + juce::Point<float> (9.0f, -9.0f), r.getBottomRight() + juce::Point<float> (-9.0f, -9.0f) })
            Pequod::hw::screw (g, p, 3.0f, p.x * 0.13f);
    }

    juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

//==============================================================================
juce::Rectangle<float> Knob::knobArea() const
{
    // the knob and its readout together, centred in what's left under the label
    auto r = getLocalBounds().toFloat();
    if (label.isNotEmpty()) r.removeFromTop (15.0f);
    const float textH = text ? (small ? 13.0f : 16.0f) : 0.0f;
    const float d = juce::jmin (r.getWidth(), r.getHeight() - textH, small ? 46.0f : 52.0f);
    return juce::Rectangle<float> (d, d).withCentre ({ r.getCentreX(), r.getCentreY() - textH * 0.5f });
}

void Knob::paint (juce::Graphics& g)
{
    const float v = get ? get() : 0.0f;
    auto b = getLocalBounds().toFloat();
    if (label.isNotEmpty())
    {
        g.setFont (Pequod::heading (11.0f));
        Pequod::hw::engraved (g, label, b.removeFromTop (15.0f), ink, juce::Justification::centred);
    }
    const auto k = knobArea();
    if (text)
    {
        g.setFont (small ? Pequod::body (10.0f) : Pequod::heading (11.5f, false));
        g.setColour (small ? inkDim : ink.withAlpha (0.85f));
        g.drawText (text (v), b.withY (k.getBottom()).withHeight (small ? 13.0f : 16.0f), juce::Justification::centred, false);
    }

    const auto c = k.getCentre();
    const float R = k.getWidth() * 0.5f;

    // the printed scale
    const int ticks = choices > 1 && choices <= 13 ? choices : 11;
    g.setColour (ink.withAlpha (0.55f));
    for (int t = 0; t < ticks; ++t)
    {
        const float a = arcStart + (arcEnd - arcStart) * (float) t / (float) (ticks - 1);
        const float r0 = R * 0.86f, r1 = R * (t == 0 || t == ticks - 1 ? 1.0f : 0.95f);
        g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, 1.0f);
    }

    // the skirt (knurled), then the cap
    const float rs = R * 0.78f, rc = R * 0.56f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - rs, c.y - rs + 2.0f, rs * 2.0f, rs * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3c), c.x, c.y - rs, juce::Colour (0xff0b0b0c), c.x, c.y + rs, false));
    g.fillEllipse (c.x - rs, c.y - rs, rs * 2.0f, rs * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int t = 0; t < 36; ++t)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) t / 36.0f;
        g.drawLine (c.x + std::sin (a) * rc, c.y - std::cos (a) * rc, c.x + std::sin (a) * rs, c.y - std::cos (a) * rs, 0.8f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c2e), c.x - rc * 0.4f, c.y - rc, juce::Colour (0xff070708), c.x + rc * 0.3f, c.y + rc, false));
    g.fillEllipse (c.x - rc, c.y - rc, rc * 2.0f, rc * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (c.x - rc, c.y - rc, rc * 2.0f, rc * 2.0f, 0.8f);

    // the line
    const float a = arcStart + (arcEnd - arcStart) * juce::jlimit (0.0f, 1.0f, v);
    g.setColour (juce::Colour (0xfff4f0e6));
    g.drawLine (c.x + std::sin (a) * rc * 0.2f, c.y - std::cos (a) * rc * 0.2f, c.x + std::sin (a) * rs * 0.95f, c.y - std::cos (a) * rs * 0.95f,
                juce::jmax (1.5f, R * 0.09f));
}

void Knob::change (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (choices > 1) v = (float) juce::roundToInt (v * (float) (choices - 1)) / (float) (choices - 1);
    if (set) set (v);
    refresh();
}

void Knob::mouseDown (const juce::MouseEvent&)    { dragStart = get ? get() : 0.0f; }
void Knob::mouseDoubleClick (const juce::MouseEvent&) { change (normal); }

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    const float travel = e.mods.isShiftDown() ? 900.0f : choices > 16 ? 600.0f : 180.0f;
    change (dragStart + (float) -e.getDistanceFromDragStartY() / travel + (float) e.getDistanceFromDragStartX() / (travel * 2.0f));
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float now = get ? get() : 0.0f;
    const float dir = (w.deltaY + w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
    if (dir == 0.0f) return;
    if (choices > 1) change (now + (dir > 0.0f ? 1.0f : -1.0f) / (float) (choices - 1));
    else change (now + dir * (e.mods.isShiftDown() ? 0.02f : 0.08f));
}

bool Knob::refresh()
{
    const float v = get ? get() : 0.0f;
    if (v == shown) return false;
    shown = v;
    repaint();
    return true;
}

//==============================================================================
/** A step's switch: a lever in a slot with four detents, PLAY at the top, SKIP at the bottom. */
class View::ModeSwitch : public juce::Component
{
public:
    ModeSwitch (Engine*& e, int s) : engine (e), step (s) {}

    int mode() const { return engine->now().steps[(size_t) step].mode.load(); }
    void setMode (int m) { engine->now().steps[(size_t) step].mode = juce::jlimit (0, 3, m); shown = -1; refresh(); }
    bool refresh() { const int m = mode(); if (m == shown) return false; shown = m; repaint(); return true; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
        const auto names = Engine::modeNames();
        auto slotR = r.removeFromLeft (juce::jmin (12.0f, r.getWidth() * 0.3f));
        Pequod::hw::slot (g, slotR.withSizeKeepingCentre (5.0f, slotR.getHeight() - 6.0f));
        const float h = r.getHeight() / 4.0f;
        const int m = mode();
        for (int k = 0; k < 4; ++k)
        {
            const auto row = juce::Rectangle<float> (r.getX() + 3.0f, r.getY() + h * (float) k, r.getWidth() - 3.0f, h);
            g.setFont (Pequod::heading (9.5f, k == m));
            g.setColour (k == m ? (k == Engine::play ? ink : partui::accent ("poe")) : inkDim.withAlpha (0.8f));
            g.drawText (names[k], row, juce::Justification::centredLeft, false);
        }
        // the lever's head at its detent
        const auto c = juce::Point<float> (slotR.getCentreX(), r.getY() + h * ((float) m + 0.5f));
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (juce::Rectangle<float> (11.0f, 7.0f).withCentre (c.translated (0.0f, 1.5f)), 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e8e4), c.x, c.y - 4.0f, juce::Colour (0xff6d6d6a), c.x, c.y + 4.0f, false));
        g.fillRoundedRectangle (juce::Rectangle<float> (11.0f, 7.0f).withCentre (c), 2.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            const auto names = Engine::modeNames();
            for (int k = 0; k < 4; ++k) menu.addItem (k + 1, names[k], true, k == mode());
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r) { if (r > 0) setMode (r - 1); });
            return;
        }
        pick (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { pick (e); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (w.deltaY != 0.0f) setMode (mode() + (w.deltaY > 0.0f ? -1 : 1));
    }

private:
    void pick (const juce::MouseEvent& e)
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
        setMode ((int) ((e.position.y - r.getY()) / (r.getHeight() / 4.0f)));
    }
    Engine*& engine;
    int step, shown = -1;
};

/** One of the eight pattern keys. */
class View::PatternKey : public partui::Key
{
public:
    PatternKey (int i) : partui::Key (juce::String (i + 1), partui::Icon::none, View::accent()) { setTooltip ("Pattern " + juce::String (i + 1)); }
};

//==============================================================================
View::View()
{
    setOpaque (true);
    runKey.setClickingTogglesState (true);
    runKey.onClick = [this] { cur->run = runKey.getToggleState(); changed(); };
    randomKey.setButtonText ("RND");
    randomKey.onClick = [this] { cur->randomise (cur->current, false); changed(); };
    patternKey.onClick = [this] { showPatternMenu(); };
    for (auto* k : { &runKey, &randomKey, &patternKey }) addAndMakeVisible (k);

    for (int i = 0; i < Rack::numSequencers; ++i)
    {
        auto* k = seqKeys.add (new partui::Key ("Sequencer " + juce::String (i + 1) + ": show it here (its lamp is lit while it plays)",
                                                partui::Icon::none, accent()));
        k->setButtonText (juce::String (i + 1));
        k->onClick = [this, i] { select (i); };
        addAndMakeVisible (k);
    }
    runningShown.fill (-2);

    for (int i = 0; i < Engine::numPatterns; ++i)
    {
        auto* k = patternKeys.add (new PatternKey (i));
        k->onClick = [this, i] { cur->current = i; refreshAll(); changed(); };
        addAndMakeVisible (k);
    }

    // the programmer's knobs
    auto add = [this] (const juce::String& label, int choices, float normal, std::function<float()> get, std::function<void (float)> set,
                       std::function<juce::String (float)> text, const juce::String& tip)
    {
        auto* k = settings.add (new Knob());
        k->label = label; k->choices = choices; k->normal = normal;
        k->get = std::move (get);
        k->set = [this, s = std::move (set)] (float v) { s (v); changed(); };
        k->text = std::move (text);
        k->setTooltip (tip);
        addAndMakeVisible (k);
    };
    auto pick = [] (float v, int n) { return juce::roundToInt (v * (float) (n - 1)); };
    auto at = [] (int i, int n) { return n > 1 ? (float) i / (float) (n - 1) : 0.0f; };

    add ("LENGTH", 16, 1.0f, [this, at] { return at (cur->now().length.load() - 1, 16); },
         [this, pick] (float v) { cur->now().length = pick (v, 16) + 1; },
         [pick] (float v) { return juce::String (pick (v, 16) + 1) + " STEPS"; },
         "How many steps the pattern runs (click a step's lamp to end it there)");
    add ("RATE", 6, 0.6f, [this, at] { return at (cur->rate.load(), 6); }, [this, pick] (float v) { cur->rate = pick (v, 6); },
         [pick] (float v) { return Engine::rateNames()[pick (v, 6)]; }, "How long each step is, at MELVILLE's tempo");
    add ("DIRECTION", 4, 0.0f, [this, at] { return at (cur->direction.load(), 4); }, [this, pick] (float v) { cur->direction = pick (v, 4); },
         [pick] (float v) { return Engine::directionNames()[pick (v, 4)].toUpperCase(); },
         "Forward, backward, back and forth, or a random step each time (the same at the same place on tape)");
    add ("SWING", 0, 0.0f, [this] { return cur->swing.load() * 2.0f; }, [this] (float v) { cur->swing = v * 0.5f; },
         [] (float v) { return juce::String (50 + juce::roundToInt (v * 25.0f)) + "%"; },
         "Swing: every other step comes late (50% straight, 66% a triplet shuffle, 75% dotted)");
    add ("GLIDE", 0, 0.0f, [this] { return std::sqrt (cur->glideMs.load() / 1000.0f); }, [this] (float v) { cur->glideMs = v * v * 1000.0f; },
         [] (float v) { return v <= 0.0f ? juce::String ("OFF") : juce::String (juce::roundToInt (v * v * 1000.0f)) + " ms"; },
         "Portamento on the pitch CV (as the 960's glide): how long it takes to slide to each new note");
    add ("CHANNEL", 16, 0.0f, [this, at] { return at (cur->channel.load() - 1, 16); }, [this, pick] (float v) { cur->channel = pick (v, 16) + 1; },
         [pick] (float v) { return "MIDI " + juce::String (pick (v, 16) + 1); }, "The MIDI channel POE plays on");
    add ("SCALE", (int) Engine::scaleNames().size(), at (1, Engine::scaleNames().size()),
         [this, at] { return at (cur->scale.load(), Engine::scaleNames().size()); },
         [this, pick] (float v) { cur->scale = pick (v, Engine::scaleNames().size()); },
         [pick] (float v) { return Engine::scaleNames()[pick (v, Engine::scaleNames().size())].toUpperCase(); },
         "The scale row A's knobs are quantised to");
    add ("ROOT", 12, 0.0f, [this, at] { return at (cur->root.load(), 12); }, [this, pick] (float v) { cur->root = pick (v, 12); },
         [pick] (float v) { return Engine::noteName (pick (v, 12) + 12).dropLastCharacters (1); }, "The scale's key note");
    add ("OCTAVE", 8, at (3, 8), [this, at] { return at (cur->octave.load(), 8); }, [this, pick] (float v) { cur->octave = pick (v, 8); },
         [pick] (float v) { return "FROM C" + juce::String (pick (v, 8)); }, "Where row A's knobs start (turned fully down)");
    add ("RANGE", 4, at (1, 4), [this, at] { return at (cur->range.load() - 1, 4); }, [this, pick] (float v) { cur->range = pick (v, 4) + 1; },
         [pick] (float v) { const int n = pick (v, 4) + 1; return juce::String (n) + (n == 1 ? " OCTAVE" : " OCTAVES"); },
         "How many octaves row A's knobs cover");
    add ("ROW B", 129, 0.0f, [this, at] { return at (cur->bCc.load() + 1, 129); }, [this, pick] (float v) { cur->bCc = pick (v, 129) - 1; },
         [pick] (float v) { const int n = pick (v, 129) - 1; return n < 0 ? juce::String ("VELOCITY") : "CC " + juce::String (n); },
         "What row B sends: each note's velocity, or a MIDI CC (CV B always carries it, 0-5 V)");
    add ("ROW C", 129, 0.0f, [this, at] { return at (cur->cCc.load() + 1, 129); }, [this, pick] (float v) { cur->cCc = pick (v, 129) - 1; },
         [pick] (float v) { const int n = pick (v, 129) - 1; return n < 0 ? juce::String ("GATE TIME") : "CC " + juce::String (n); },
         "What row C sends: how long each note holds, or a MIDI CC (CV C always carries it, 0-5 V)");
    add ("TRANSPOSE", 2, 1.0f, [this] { return cur->follows.load() ? 1.0f : 0.0f; }, [this] (float v) { cur->follows = v > 0.5f; },
         [] (float v) { return v > 0.5f ? juce::String ("FOLLOWS KEY") : juce::String ("FIXED"); },
         "Whether this sequencer follows the TRANSPOSE jack's keyboard (a bass line that does, a drone that doesn't)");
    static const float scales[] = { 5.0f, 8.0f, 10.0f, 12.0f, 15.0f, 20.0f };
    add ("CV SCALE", 6, at (2, 6),
         [this, at] { int i = 0; for (int k = 0; k < 6; ++k) if (std::abs (cur->fullScale.load() - scales[k]) < 0.01f) i = k; return at (i, 6); },
         [this, pick] (float v) { cur->fullScale = scales[pick (v, 6)]; },
         [pick] (float v) { return juce::String (juce::roundToInt (scales[pick (v, 6)])) + " V FULL"; },
         "Your interface's output at full scale, in volts (DC-coupled outputs; see its manual): so 1 V an octave comes out as 1 V");

    // the steps
    for (int i = 0; i < Engine::numSteps; ++i)
    {
        for (int row = 0; row < 3; ++row)
        {
            auto k = std::make_unique<Knob>();
            k->small = true;
            k->normal = row == 0 ? 0.0f : row == 1 ? 0.75f : 0.5f;
            k->get = [this, i, row]
            {
                auto& s = cur->now().steps[(size_t) i];
                return (row == 0 ? s.a : row == 1 ? s.b : s.c).load();
            };
            k->set = [this, i, row] (float v)
            {
                auto& s = cur->now().steps[(size_t) i];
                (row == 0 ? s.a : row == 1 ? s.b : s.c) = v;
                changed();
            };
            if (row == 1) k->text = [] (float v) { return juce::String (juce::roundToInt (v * 127.0f)); };
            if (row == 2) k->text = [this] (float v) { return cur->cCc.load() < 0 ? percent (juce::jmap (v, 0.05f, 1.0f)) : juce::String (juce::roundToInt (v * 127.0f)); };
            k->setTooltip (row == 0 ? "Row A: this step's note (shown above it)" : row == 1 ? "Row B: velocity (or the CC it's set to)"
                                                                                     : "Row C: how long the note holds (or the CC it's set to)");
            addAndMakeVisible (*k);
            stepKnobs[(size_t) i][(size_t) row] = std::move (k);
        }
        addAndMakeVisible (modes.add (new ModeSwitch (cur, i)));
    }
    notesShown.fill (-1);

    // the CV and MIDI: MELVILLE's POE device (its CV outs on the patchbay); its MIDI on the MIDI tab
    rack.transposeJack = melville::midiJack ("POE transpose");
    melville::attach (deviceManager, "poe");
    deviceManager.addAudioCallback (&rack);
    melville::addToSession (deviceManager, {
        [this] { return rack.toXml(); },
        [this] (const juce::XmlElement& x) { rack.fromXml (x); refreshAll(); },
        [this] (const melville::NewStudio&) { rack.fromXml (juce::XmlElement ("POE")); refreshAll(); } });

    // (development: POE_DEMO=1 sets a pattern up and RUN on, for pictures)
    if (juce::SystemStats::getEnvironmentVariable ("POE_DEMO", {}).isNotEmpty())
    {
        const float a[] = { 0.0f, 0.0f, 0.43f, 0.0f, 0.29f, 0.0f, 0.5f, 0.36f, 0.0f, 0.0f, 0.43f, 0.0f, 0.64f, 0.57f, 0.5f, 0.36f };
        const int m[] = { 0, 1, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 3, 0 };
        auto& p = cur->pattern (0);
        for (int i = 0; i < Engine::numSteps; ++i)
        {
            p.steps[(size_t) i].a = a[i];
            p.steps[(size_t) i].b = 0.55f + 0.45f * (float) ((i * 7) % 5) / 4.0f;
            p.steps[(size_t) i].c = (i % 4 == 0) ? 0.85f : 0.35f;
            p.steps[(size_t) i].mode = m[i];
        }
        cur->scale = 2;
        cur->root = 9;
        cur->octave = 2;
        cur->run = true;
    }

    refreshAll();
    startTimerHz (30);
}

View::~View()
{
    stopTimer();
    deviceManager.removeAudioCallback (&rack);
    melville::detach (deviceManager);
}

void View::changed() { refreshAll(); }

void View::select (int i)
{
    selected = juce::jlimit (0, Rack::numSequencers - 1, i);
    cur = &rack.seq (selected);
    notesShown.fill (-1);
    litShown = -2;
    for (auto* m : modes) m->refresh();
    refreshAll();
}

void View::refreshAll()
{
    runKey.setToggleState (cur->run.load(), juce::dontSendNotification);
    for (int i = 0; i < seqKeys.size(); ++i) seqKeys[i]->setToggleState (i == selected, juce::dontSendNotification);
    for (int i = 0; i < patternKeys.size(); ++i) patternKeys[i]->setToggleState (i == cur->current.load(), juce::dontSendNotification);
    for (auto* k : settings) k->refresh();
    bool any = false;
    for (auto& col : stepKnobs) for (auto& k : col) { k->repaint(); k->refresh(); }
    for (auto* m : modes) any |= m->refresh();
    juce::ignoreUnused (any);
    repaint();
}

void View::timerCallback()
{
    for (auto* k : settings) k->refresh();
    for (auto& col : stepKnobs) for (auto& k : col) k->refresh();
    bool redraw = false;
    for (auto* m : modes) m->refresh();
    if (runKey.getToggleState() != cur->run.load()) runKey.setToggleState (cur->run.load(), juce::dontSendNotification);
    const int lit = cur->lit.load(), tr = cur->follows.load() ? cur->transpose.load() : 0, sn = cur->soundingNote.load();
    if (lit != litShown || tr != transposeShown || sn != noteShown) { litShown = lit; transposeShown = tr; noteShown = sn; redraw = true; }
    for (int i = 0; i < Engine::numSteps; ++i)
    {
        const auto& s = cur->now().steps[(size_t) i];
        const int code = cur->noteFor (s.a.load()) + tr + 1000 * s.mode.load() + 5000 * (i < cur->now().length.load() ? 1 : 0);
        if (code != notesShown[(size_t) i]) { notesShown[(size_t) i] = code; redraw = true; }
    }
    if (redraw) repaint (stepsArea);
    for (int i = 0; i < Rack::numSequencers; ++i)
    {
        const int on = rack.seq (i).lit.load() >= 0 ? 1 : rack.seq (i).run.load() ? 0 : -1;
        if (on != runningShown[(size_t) i]) { runningShown[(size_t) i] = on; repaint (headerArea); }
    }
    repaint (statusArea);
}

juce::String View::statusLine() const
{
    const auto& t = melville::transport();
    const bool rolling = t.rolling.load();
    juce::String s = "SEQUENCER " + juce::String (selected + 1) + "   ";
    s << (cur->run.load() ? (rolling ? "RUNNING" : "RUN - WAITING FOR THE TRANSPORT") : "STOPPED");
    s << "   PATTERN " << (cur->current.load() + 1) << "   " << Engine::rateNames()[cur->rate.load()]
      << "   " << juce::String (t.bpm.load(), 1) << " BPM";
    if (const int tr = cur->transpose.load(); tr != 0 && cur->follows.load()) s << "   TRANSPOSE " << (tr > 0 ? "+" : "") << tr;
    return s;
}

void View::showPatternMenu()
{
    juce::PopupMenu copy, toSeq, menu;
    const int pat = cur->current.load();
    for (int k = 0; k < Engine::numPatterns; ++k)
        if (k != pat) copy.addItem (100 + k, "Pattern " + juce::String (k + 1));
    for (int j = 0; j < Rack::numSequencers; ++j)
        if (j != selected) toSeq.addItem (200 + j, "Sequencer " + juce::String (j + 1) + ", pattern " + juce::String (pat + 1));
    menu.addSubMenu ("Copy pattern " + juce::String (pat + 1) + " to", copy);
    menu.addSubMenu ("Copy pattern " + juce::String (pat + 1) + " to another sequencer", toSeq);
    menu.addItem (1, "Random pitches (keep gates and switches)");
    menu.addItem (2, "Random everything");
    menu.addSeparator();
    menu.addItem (3, "Clear pattern " + juce::String (pat + 1));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternKey), [this, pat] (int r)
    {
        if (r >= 200)
        {
            auto& from = cur->pattern (pat);
            auto& to = rack.seq (r - 200).pattern (pat);
            to.length = from.length.load();
            for (int i = 0; i < Engine::numSteps; ++i)
            {
                auto& a = from.steps[(size_t) i];
                auto& b = to.steps[(size_t) i];
                b.a = a.a.load(); b.b = a.b.load(); b.c = a.c.load(); b.mode = a.mode.load();
            }
            return;
        }
        if (r >= 100) { cur->copyPattern (pat, r - 100); return; }
        if (r == 1) cur->randomise (pat, true);
        if (r == 2) cur->randomise (pat, false);
        if (r == 3) cur->clearPattern (pat);
        if (r > 0) changed();
    });
}

void View::mouseDown (const juce::MouseEvent& e)
{
    // a step's lamp: the pattern ends there (click the last one lit to run all sixteen)
    for (int i = 0; i < Engine::numSteps; ++i)
        if (lampArea[(size_t) i].contains (e.getPosition()))
        {
            auto& len = cur->now().length;
            len = (len.load() == i + 1) ? Engine::numSteps : i + 1;
            changed();
            return;
        }
}

//==============================================================================
void View::paint (juce::Graphics& g)
{
    g.fillAll (Pequod::Colours::bg);
    partui::drawBar (g, headerArea, accent());
    partui::drawWordmark (g, headerArea.reduced (18, 0).removeFromLeft (190), "POE", "STEP SEQUENCER", accent(), Pequod::heading (30.0f));
    g.setFont (Pequod::heading (13.0f));
    g.setColour (accent());
    g.drawText (statusLine(), statusArea.withHeight (statusArea.getHeight() / 2).translated (0, 4), juce::Justification::centredLeft, true);
    g.setFont (Pequod::body (12.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawText ("Plays while RUN is lit and MELVILLE's transport rolls. Cable its MIDI OUT to an ALCOTT slot or a MIDI OUT on the PATCHBAY's MIDI tab; its CV outs are on the audio tab",
                statusArea.withTrimmedTop (statusArea.getHeight() / 2).translated (0, -2), juce::Justification::centredLeft, true);

    // each sequencer key's lamp: lit while it plays, dim while it's set to run
    for (int i = 0; i < seqKeys.size(); ++i)
    {
        const auto kb = seqKeys[i]->getBounds().toFloat();
        const int st = runningShown[(size_t) i];
        if (st >= 0) Pequod::hw::lamp (g, { kb.getCentreX(), kb.getBottom() + 5.0f }, 2.5f, st > 0 ? juce::Colour (0xffff4a2e) : accent().withAlpha (0.5f), st > 0);
    }

    // the cabinet: walnut cheeks either side
    const auto body = programmerArea.getUnion (stepsArea).toFloat();
    cheek (g, juce::Rectangle<float> (body.getX() - 16.0f, body.getY() - 2.0f, 14.0f, body.getHeight() + 4.0f));
    cheek (g, juce::Rectangle<float> (body.getRight() + 2.0f, body.getY() - 2.0f, 14.0f, body.getHeight() + 4.0f));

    // the programmer
    facePanel (g, programmerArea.toFloat());
    g.setFont (Pequod::heading (12.0f));
    Pequod::hw::engraved (g, "PATTERN", patternArea.withHeight (16).translated (0, -18).toFloat(), ink, juce::Justification::centredLeft);
    g.setColour (ink.withAlpha (0.25f));
    g.drawHorizontalLine (patternArea.getBottom() + 10, (float) programmerArea.getX() + 14.0f, (float) programmerArea.getRight() - 14.0f);
    g.setFont (Pequod::heading (10.0f, false));
    Pequod::hw::engraved (g, "SHALLOWMOUSE  MODEL 16  SEQUENTIAL CONTROLLER", programmerArea.toFloat().removeFromBottom (24.0f).reduced (14.0f, 0.0f),
                          inkDim, juce::Justification::centred);

    // the steps
    facePanel (g, stepsArea.toFloat());
    auto& p = cur->now();
    const int len = p.length.load();
    const int lit = cur->lit.load();
    const int tr = cur->follows.load() ? cur->transpose.load() : 0;
    for (int i = 0; i < Engine::numSteps; ++i)
    {
        const auto col = columnArea[(size_t) i];
        const bool in = i < len;
        if (! in)
        {
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillRect (col.withTop (rowArea[1].getY()).withBottom (rowArea[5].getBottom()).reduced (2, 0));
        }
        if (i > 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawVerticalLine (col.getX(), (float) rowArea[1].getY(), (float) rowArea[5].getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.drawVerticalLine (col.getX() + 1, (float) rowArea[1].getY(), (float) rowArea[5].getBottom());
        }
        // its number and lamp
        auto la = lampArea[(size_t) i].toFloat();
        g.setFont (Pequod::heading (12.0f));
        Pequod::hw::engraved (g, juce::String (i + 1), la.removeFromLeft (la.getWidth() * 0.45f), in ? ink : inkDim, juce::Justification::centredRight);
        Pequod::hw::lamp (g, { la.getCentreX(), la.getCentreY() }, 5.0f, lampRed, i == lit);

        // its note
        const auto rd = readoutArea[(size_t) i].toFloat();
        Pequod::hw::window (g, rd);
        const int mode = p.steps[(size_t) i].mode.load();
        const auto note = Engine::noteName (juce::jlimit (0, 127, cur->noteFor (p.steps[(size_t) i].a.load()) + tr));
        const auto shown = mode == Engine::rest ? juce::String ("--") : mode == Engine::skip ? juce::String ("  ") : note;
        g.setFont (Pequod::heading (13.0f));
        Pequod::hw::lit (g, shown, rd.reduced (2.0f, 1.0f), (in ? accent() : accent().withMultipliedBrightness (0.5f)).withAlpha (mode == Engine::tie ? 0.75f : 1.0f),
                         juce::Justification::centred, "888");
        Pequod::hw::glass (g, rd);
    }

    // the rows' legends, down the left
    auto legend = [&] (int row, const juce::String& letter, const juce::String& what)
    {
        auto r = rowArea[(size_t) row].withWidth (columnArea[0].getX() - stepsArea.getX()).reduced (12, 0).toFloat();
        g.setFont (Pequod::heading (24.0f));
        Pequod::hw::engraved (g, letter, r.removeFromTop (r.getHeight() * 0.55f), accent(), juce::Justification::bottomLeft);
        g.setFont (Pequod::heading (10.0f));
        Pequod::hw::engraved (g, what, r, ink, juce::Justification::topLeft);
    };
    const int bc = cur->bCc.load(), cc = cur->cCc.load();
    g.setFont (Pequod::heading (10.0f));
    Pequod::hw::engraved (g, "STEP", rowArea[0].withWidth (columnArea[0].getX() - stepsArea.getX()).reduced (12, 0).toFloat(), ink, juce::Justification::centredLeft);
    Pequod::hw::engraved (g, "NOTE", rowArea[1].withWidth (columnArea[0].getX() - stepsArea.getX()).reduced (12, 0).toFloat(), ink, juce::Justification::centredLeft);
    legend (2, "A", "PITCH");
    legend (3, "B", bc < 0 ? juce::String ("VELOCITY") : "CC " + juce::String (bc));
    legend (4, "C", cc < 0 ? juce::String ("GATE") : "CC " + juce::String (cc));
    Pequod::hw::engraved (g, "MODE", rowArea[5].withWidth (columnArea[0].getX() - stepsArea.getX()).reduced (12, 0).toFloat(), ink, juce::Justification::centredLeft);
}

void View::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (partui::headerHeight);
    auto bar = headerArea.withTrimmedBottom (2).reduced (14, 0);
    bar.removeFromLeft (200);
    bar = partui::placeKeys (bar, { &patternKey, &randomKey, &runKey, seqKeys[3], seqKeys[2], seqKeys[1], seqKeys[0] });
    statusArea = bar.withTrimmedRight (10).withSizeKeepingCentre (bar.getWidth() - 10, 40);

    r.reduce (26, 10);       // (room for the cheeks)
    programmerArea = r.removeFromLeft (300);
    r.removeFromLeft (8);
    stepsArea = r;

    // the programmer: the pattern keys, then the knobs three across
    auto pr = programmerArea.reduced (16, 14);
    pr.removeFromTop (20);
    patternArea = pr.removeFromTop (70);
    {
        auto keys = patternArea;
        const int kw = (keys.getWidth() - 3 * 6) / 4, kh = 32;
        for (int i = 0; i < patternKeys.size(); ++i)
            patternKeys[i]->setBounds (keys.getX() + (i % 4) * (kw + 6), keys.getY() + (i / 4) * (kh + 6), kw, kh);
    }
    pr.removeFromTop (22);
    pr.removeFromBottom (22);
    const int rows = (settings.size() + 2) / 3;
    const int cellH = juce::jmin (96, pr.getHeight() / rows), cellW = pr.getWidth() / 3;
    for (int i = 0; i < settings.size(); ++i)
    {
        const int row = i / 3, colN = i % 3;
        const int x = (i == settings.size() - 1 && settings.size() % 3 == 1) ? pr.getX() + cellW : pr.getX() + colN * cellW;     // (the last one centred)
        settings[i]->setBounds (juce::Rectangle<int> (x, pr.getY() + row * cellH, cellW, cellH).reduced (4, 3));
    }

    // the steps: a column each, after the rows' legends
    auto sr = stepsArea.reduced (10, 16);
    const int legendW = 74;
    sr.removeFromLeft (legendW);
    const int rowsH[] = { 30, 30 };
    rowArea[0] = stepsArea.withY (sr.getY()).withHeight (rowsH[0]);
    rowArea[1] = rowArea[0].translated (0, rowsH[0] + 4).withHeight (rowsH[1]);
    const int switchH = 74;
    const int knobsTop = rowArea[1].getBottom() + 8, knobsH = (sr.getBottom() - switchH - 6 - knobsTop) / 3;
    for (int k = 0; k < 3; ++k) rowArea[(size_t) (2 + k)] = stepsArea.withY (knobsTop + k * knobsH).withHeight (knobsH);
    rowArea[5] = stepsArea.withY (sr.getBottom() - switchH).withHeight (switchH);

    const float colW = (float) sr.getWidth() / (float) Engine::numSteps;
    for (int i = 0; i < Engine::numSteps; ++i)
    {
        const int x0 = sr.getX() + juce::roundToInt (colW * (float) i), x1 = sr.getX() + juce::roundToInt (colW * (float) (i + 1));
        auto col = juce::Rectangle<int> (x0, sr.getY(), x1 - x0, sr.getHeight());
        columnArea[(size_t) i] = col;
        lampArea[(size_t) i] = rowArea[0].withX (col.getX()).withWidth (col.getWidth()).reduced (2, 2);
        readoutArea[(size_t) i] = rowArea[1].withX (col.getX()).withWidth (col.getWidth()).reduced (5, 3);
        for (int k = 0; k < 3; ++k)
        {
            const auto cell = rowArea[(size_t) (2 + k)].withX (col.getX()).withWidth (col.getWidth()).reduced (1, 2);
            stepKnobs[(size_t) i][(size_t) k]->setBounds (cell);
        }
        modes[i]->setBounds (rowArea[5].withX (col.getX()).withWidth (col.getWidth()).reduced (3, 2));
    }
}

} // namespace poe
