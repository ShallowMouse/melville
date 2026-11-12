#include "AlcottView.h"
#include "PequodPanel.h"
#include <MelvilleHosted.h>

namespace alcott
{

// The rack, as EMERSON draws it: 1U = 44.45 mm of a 482.6 mm wide panel, ears included
static constexpr double rackWidthMm = 482.6, unitMm = 44.45, earMm = 15.88, holeSpanMm = 31.75;
static int earFor (int width)     { return juce::roundToInt (width * (earMm / rackWidthMm)); }
static double unitFor (int width) { return width * (unitMm / rackWidthMm); }

static juce::String midiFrom (Engine& e, int slot)
{
    const auto from = melville::midiCabledInto (e.jack (slot));
    return from.isNotEmpty() ? "MIDI from " + from : juce::String ("No MIDI cabled in");
}

//==============================================================================
/** The eight slots: number, instrument, where its notes come from, a meter. */
class View::SlotList : public juce::Component
{
public:
    explicit SlotList (View& v) : owner (v) {}

    static constexpr int rowH = 58;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Pequod::Colours::panel);
        for (int i = 0; i < Engine::numSlots; ++i)
        {
            auto& s = owner.engine.slot (i);
            auto r = juce::Rectangle<int> (0, i * rowH, getWidth(), rowH - 2);
            const bool sel = i == owner.current;
            g.setColour (sel ? Pequod::Colours::well.brighter (0.15f) : Pequod::Colours::well);
            g.fillRect (r);
            if (sel) { g.setColour (accent()); g.fillRect (r.removeFromLeft (3)); } else r.removeFromLeft (3);

            auto num = r.removeFromLeft (34);
            g.setFont (Pequod::heading (16.0f));
            g.setColour (sel ? accent() : Pequod::Colours::textDim);
            g.drawText (juce::String (i + 1), num, juce::Justification::centred);

            // the meter and the note lamp, right
            auto m = r.removeFromRight (16).reduced (4, 8);
            g.setColour (juce::Colours::black);
            g.fillRect (m);
            const float pk = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (shown[(size_t) i], -60.0f) + 60.0f) / 66.0f);
            g.setColour (pk > 0.95f ? juce::Colour (0xffd04a3a) : accent());
            g.fillRect (m.withTrimmedTop (juce::roundToInt ((float) m.getHeight() * (1.0f - pk))));
            auto lamp = r.removeFromRight (16);
            g.setColour (accent().withAlpha (0.15f + 0.85f * lamps[(size_t) i]));
            g.fillEllipse (lamp.toFloat().withSizeKeepingCentre (7.0f, 7.0f));

            r.reduce (4, 8);
            const auto name = s.shownName();
            g.setFont (Pequod::body (14.0f, true));
            g.setColour (name.isEmpty() ? Pequod::Colours::textDim : Pequod::Colours::text);
            g.drawText (name.isEmpty() ? juce::String ("Empty") : name, r.removeFromTop (r.getHeight() / 2), juce::Justification::centredLeft, true);
            g.setFont (Pequod::body (11.5f));
            g.setColour (Pequod::Colours::textDim);
            juce::String sub = s.instrument == nullptr ? juce::String ("Click to choose an instrument")
                             : midiFrom (owner.engine, i) + (s.channel.load() > 0 ? "  ch " + juce::String (s.channel.load()) : juce::String())
                               + (s.effects.empty() ? juce::String() : "  +" + juce::String ((int) s.effects.size()) + " fx")
                               + (s.mute.load() ? juce::String ("  MUTED") : juce::String());
            g.drawText (sub, r, juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = e.y / rowH;
        if (! juce::isPositiveAndBelow (i, Engine::numSlots)) return;
        owner.select (i);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Rename...");
            m.addItem (2, "Clear this slot", owner.engine.slot (i).instrument != nullptr || ! owner.engine.slot (i).effects.empty());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, i] (int r)
            {
                if (r == 1) rename (i);
                if (r == 2) owner.clear.triggerClick();
            });
        }
        else if (owner.engine.slot (i).instrument == nullptr)
            owner.showInstrumentMenu();
    }

    void rename (int i)
    {
        auto* w = new juce::AlertWindow ("Rename slot " + juce::String (i + 1), "A name for this slot (empty: the instrument's).", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", owner.engine.slot (i).name);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, i, w] (int r)
        {
            if (r == 1) { owner.engine.slot (i).name = w->getTextEditorContents ("name").trim(); owner.changed(); }
        }), true);
    }

    void tick()
    {
        for (int i = 0; i < Engine::numSlots; ++i)
        {
            auto& s = owner.engine.slot (i);
            shown[(size_t) i] = juce::jmax (s.peak.exchange (0.0f), shown[(size_t) i] * 0.8f);
            lamps[(size_t) i] = juce::jmax (s.noteLamp.exchange (0.0f), lamps[(size_t) i] * 0.7f);
        }
        repaint();
    }

private:
    View& owner;
    std::array<float, Engine::numSlots> shown {}, lamps {};
};

//==============================================================================
/** A PEQUOD panel mounted in the rack: power and remove on the left ear, move up / down on the right. */
class View::Unit : public juce::Component, private juce::ComponentListener
{
public:
    Unit (View& o, Engine::Unit& u, bool isInstrument) : owner (o), unit (u), instrument (isInstrument)
    {
        power.setToggleState (! u.bypassed.load(), juce::dontSendNotification);
        power.setTooltip (instrument ? "On / off" : "On / bypassed");
        power.onClick = [this] { unit.bypassed = ! power.getToggleState(); repaint(); owner.changed(); };
        addAndMakeVisible (power);
        for (auto* b : { &up, &down, &remove })
        {
            b->setWantsKeyboardFocus (false);
            b->setColour (juce::TextButton::buttonColourId, Pequod::Colours::well);
            addAndMakeVisible (b);
        }
        up.setVisible (! instrument);
        down.setVisible (! instrument);
        remove.setTooltip (instrument ? "Take the instrument out (its effects stay)" : "Take it out of the rack");
        up.setTooltip ("Earlier in the chain");
        down.setTooltip ("Later in the chain");
        up.onClick     = [this] { const int uid = unit.uid; juce::MessageManager::callAsync ([o = &owner, uid] { o->units.clear(); o->engine.moveEffect (o->current, uid, -1); o->rebuildRack(); }); };
        down.onClick   = [this] { const int uid = unit.uid; juce::MessageManager::callAsync ([o = &owner, uid] { o->units.clear(); o->engine.moveEffect (o->current, uid, +1); o->rebuildRack(); }); };
        remove.onClick = [this]
        {
            const int uid = unit.uid;
            const bool inst = instrument;
            juce::MessageManager::callAsync ([o = &owner, uid, inst]
            {
                o->units.clear();
                if (inst) o->engine.setInstrument (o->current, {});
                else      o->engine.removeEffect (o->current, uid);
                o->rebuildRack();
            });
        };

        face.setInterceptsMouseClicks (false, true);
        addAndMakeVisible (face);
        if (auto* e = unit.plugin->createEditorAndMakeActive())
        {
            editor.reset (e);
            face.addAndMakeVisible (*editor);
            editor->addComponentListener (this);
        }
    }

    ~Unit() override
    {
        if (editor != nullptr)
        {
            editor->removeComponentListener (this);
            unit.plugin->editorBeingDeleted (editor.get());
            editor = nullptr;
        }
    }

    int heightFor (int width) const
    {
        const double u = unitFor (width);
        const int faceW = width - 2 * earFor (width);
        int n = 1;
        if (editor != nullptr && editor->getWidth() > 0)
            n = juce::jmax (1, (int) std::ceil (editor->getHeight() * ((double) faceW / editor->getWidth()) / u - 0.02));
        return juce::roundToInt (u * n);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float ear = (float) earFor (getWidth());
        g.setColour (juce::Colour (0xff2a2b2e));
        g.fillRect (r);
        g.setColour (juce::Colour (0xff3a3b3f));
        g.drawRect (r, 1.0f);
        const float u = (float) unitFor (getWidth());
        const int n = juce::jmax (1, juce::roundToInt (r.getHeight() / u));
        const float half = u * (float) (holeSpanMm / unitMm) * 0.5f, screw = juce::jmax (2.5f, u * 0.055f);
        for (float x : { r.getX() + ear * 0.5f, r.getRight() - ear * 0.5f })
            for (int k : { 0, n - 1 })
            {
                const float cy = r.getY() + u * ((float) k + 0.5f);
                Pequod::drawScrew (g, { x, cy - half }, screw);
                if (n == 1 || k == n - 1) Pequod::drawScrew (g, { x, cy + half }, screw);
            }
        g.setColour (juce::Colour (0xff1c1d1f));
        g.fillRect (face.getBounds());
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (unit.bypassed.load())
        {
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRect (face.getBounds());
        }
    }

    void resized() override
    {
        const int ear = earFor (getWidth());
        auto r = getLocalBounds();
        face.setBounds (r.reduced (ear, 0));
        const int key = juce::jlimit (12, 24, ear - 6);
        auto left = r.removeFromLeft (ear), right = r.removeFromRight (ear);
        const int mid = juce::roundToInt (unitFor (getWidth()) * 0.5);
        power.setBounds  (juce::Rectangle<int> (key, key).withCentre ({ left.getCentreX(), mid - key / 2 - 2 }));
        remove.setBounds (juce::Rectangle<int> (key, key).withCentre ({ left.getCentreX(), mid + key / 2 + 2 }));
        up.setBounds     (juce::Rectangle<int> (key, key).withCentre ({ right.getCentreX(), mid - key / 2 - 2 }));
        down.setBounds   (juce::Rectangle<int> (key, key).withCentre ({ right.getCentreX(), mid + key / 2 + 2 }));
        if (editor != nullptr)
        {
            const float k = (float) face.getWidth() / (float) juce::jmax (1, editor->getWidth());
            const float y = ((float) face.getHeight() - (float) editor->getHeight() * k) * 0.5f;
            editor->setTopLeftPosition (0, 0);
            editor->setTransform (juce::AffineTransform::scale (k).translated (0.0f, y));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        if (instrument) m.addItem (1, "Change the instrument...");
        else
        {
            m.addItem (2, "Insert an effect above this");
            m.addItem (3, "Insert an effect below this");
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, uid = unit.uid] (int r)
        {
            int pos = 0;
            auto& fx = owner.engine.slot (owner.current).effects;
            for (int i = 0; i < (int) fx.size(); ++i) if (fx[(size_t) i]->uid == uid) pos = i;
            if (r == 1) owner.showInstrumentMenu();
            if (r == 2) owner.showEffectMenu (pos);
            if (r == 3) owner.showEffectMenu (pos + 1);
        });
    }

private:
    void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
    {
        if (wasResized) juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<View> (&owner)] { if (sp != nullptr) sp->layoutRack(); });
    }

    View& owner;
    Engine::Unit& unit;
    const bool instrument;
    PowerLight power;
    juce::TextButton up { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")) },
                     down { juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")) },
                     remove { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    juce::Component face;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

//==============================================================================
/** An empty 1U space in the rack, to click: choose an instrument, or add an effect. */
class View::Blank : public juce::Component
{
public:
    Blank (juce::String t, std::function<void()> f) : text (std::move (t)), onClick (std::move (f))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced ((float) earFor (getWidth()), 2.0f);
        g.setColour (isMouseOver() ? accent().withAlpha (0.18f) : Pequod::Colours::well);
        g.fillRoundedRectangle (r, 3.0f);
        juce::Path p, d;
        p.addRoundedRectangle (r, 3.0f);
        const float dash[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (d, p, dash, 2);
        g.setColour (accent().withAlpha (0.6f));
        g.fillPath (d);
        g.setFont (Pequod::heading (14.0f));
        g.setColour (accent());
        g.drawText (text, r, juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override  { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }

private:
    juce::String text;
    std::function<void()> onClick;
};

//==============================================================================
View::View()
{
    PequodPanel::setRackMounted (true);      // (the PEQUOD panels as rack faceplates, as in EMERSON)

    list = std::make_unique<SlotList> (*this);
    addAndMakeVisible (*list);

    addAndMakeVisible (midiKey);
    midiKey.onClick = [this] { showMidiInputs(); };

    channel.addItem ("Any channel / pad", 1);
    for (int c = 1; c <= 16; ++c) channel.addItem ("Channel " + juce::String (c) + (c <= 12 ? "  /  pad " + juce::String (c) : juce::String()), c + 1);
    channel.setTooltip ("Only notes on this MIDI channel play the slot. THOREAU sends pad 1 on channel 1, pad 2 on channel 2 ...");
    channel.onChange = [this] { engine.slot (current).channel = channel.getSelectedId() - 1; changed(); };
    level.setSliderStyle (juce::Slider::LinearHorizontal);
    level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    level.setRange (-60.0, 12.0, 0.1);
    level.setTextValueSuffix (" dB");
    level.setDoubleClickReturnValue (true, 0.0);
    level.onValueChange = [this] { engine.slot (current).levelDb = (float) level.getValue(); changed(); };
    mute.setClickingTogglesState (true);
    mute.onClick = [this] { engine.slot (current).mute = mute.getToggleState(); changed(); };
    clear.setTooltip ("Take the instrument and its effects out of this slot");
    clear.onClick = [this] { units.clear(); engine.clearSlot (current); rebuildRack(); };
    for (juce::Component* c : { (juce::Component*) &channel, (juce::Component*) &level,
                                (juce::Component*) &mute, (juce::Component*) &clear })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }

    rackView.setViewedComponent (&rack, false);
    rackView.setScrollBarsShown (true, false);
    rackView.setScrollBarThickness (8);
    addAndMakeVisible (rackView);
    chooseInstrument = std::make_unique<Blank> ("+  CHOOSE AN INSTRUMENT", [this] { showInstrumentMenu(); });
    addEffect = std::make_unique<Blank> ("+  ADD EFFECT", [this] { showEffectMenu (-1); });
    rack.addChildComponent (*chooseInstrument);
    rack.addAndMakeVisible (*addEffect);

    // the audio: MELVILLE's ALCOTT device (its outputs on the patchbay); every MIDI input plays it
    melville::attach (deviceManager, "alcott");
    deviceManager.addAudioCallback (&engine);
    melville::addToSession (deviceManager, {
        [this] { return engine.toXml(); },
        [this] (const juce::XmlElement& x) { units.clear(); engine.fromXml (x); rebuildRack(); list->repaint(); } });

    // (development: instruments straight away, for screenshots and checks)
    //   ALCOTT_PREVIEW="1=Juno Six|Tape Echo;2=Prophet Five"
    if (auto pv = juce::SystemStats::getEnvironmentVariable ("ALCOTT_PREVIEW", {}); pv.isNotEmpty())
        for (auto& item : juce::StringArray::fromTokens (pv, ";", ""))
        {
            const int s = item.upToFirstOccurrenceOf ("=", false, false).getIntValue() - 1;
            auto names = juce::StringArray::fromTokens (item.fromFirstOccurrenceOf ("=", false, false), "|", "");
            for (int k = 0; k < names.size(); ++k)
                for (auto& i : PequodNative::all())
                    if (i.name.equalsIgnoreCase (names[k].trim()))
                    {
                        const auto id = PequodNative::describe (i).fileOrIdentifier;
                        if (k == 0 && i.instrument) engine.setInstrument (s, id);
                        else engine.addEffect (s, id);
                        break;
                    }
        }

    // (development: ALCOTT_TEST_NOTE=1 plays every PEQUOD instrument offline, through the whole
    //  engine - a slot, its level, the THOREAU and keyboard paths - and prints what came out)
    if (juce::SystemStats::getEnvironmentVariable ("ALCOTT_TEST_NOTE", {}).isNotEmpty())
    {
        Engine t;
        t.prepareForTest (48000.0, 512);
        juce::AudioBuffer<float> out (Engine::numOutputs, 512);
        int silent = 0;
        for (auto* info : Engine::instruments())
        {
            t.setInstrument (0, PequodNative::describe (*info).fileOrIdentifier);
            float peak = 0.0f, mixPeak = 0.0f;
            for (int b = 0; b < 120; ++b)        // 1.3 s: a note held for half a second, then let go
            {
                if (b == 2)  t.handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110));
                if (b == 50) t.handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOff (1, 48));
                t.processForTest (out.getArrayOfWritePointers(), Engine::numOutputs, 512);
                peak = juce::jmax (peak, out.getMagnitude (0, 0, 512), out.getMagnitude (1, 0, 512));
                mixPeak = juce::jmax (mixPeak, out.getMagnitude (2 * Engine::numSlots, 0, 512));
                juce::Thread::sleep (1);       // (the keyboard collector times messages by the clock)
            }
            if (peak < 1.0e-3f) ++silent;
            std::printf ("ALCOTT test: %-18s slot out %.3f  mix %.3f%s\n", info->name.toRawUTF8(), peak, mixPeak, peak < 1.0e-3f ? "  SILENT" : "");
        }
        std::printf ("ALCOTT test: %d instruments, %d silent\n", (int) Engine::instruments().size(), silent);
    }

    engine.onChanged = [this] { list->repaint(); };
    select (0);
    setSize (1300, 820);
    startTimerHz (20);
}

View::~View()
{
    stopTimer();
    units.clear();                       // (the panels go before their plugins)
    deviceManager.removeAudioCallback (&engine);
    melville::detach (deviceManager);
}

void View::changed()
{
    list->repaint();
    repaint (statusArea);
}

void View::select (int s)
{
    if (s == current && ! units.isEmpty()) return;
    current = juce::jlimit (0, Engine::numSlots - 1, s);
    units.clear();
    rebuildRack();
    rackView.setViewPosition (0, 0);
}

void View::refreshControls()
{
    auto& s = engine.slot (current);
    channel.setSelectedId (s.channel.load() + 1, juce::dontSendNotification);
    level.setValue (s.levelDb.load(), juce::dontSendNotification);
    mute.setToggleState (s.mute.load(), juce::dontSendNotification);
}

void View::rebuildRack()
{
    units.clear();
    auto& s = engine.slot (current);
    if (s.instrument != nullptr) units.add (new Unit (*this, *s.instrument, true));
    for (auto& f : s.effects) units.add (new Unit (*this, *f, false));
    for (auto* u : units) rack.addAndMakeVisible (u);
    chooseInstrument->setVisible (s.instrument == nullptr);
    refreshControls();
    layoutRack();
    list->repaint();
    repaint();
}

void View::layoutRack()
{
    const int w = juce::jmax (200, rackView.getWidth() - rackView.getScrollBarThickness());
    int y = 6;
    auto& s = engine.slot (current);
    if (s.instrument == nullptr)
    {
        chooseInstrument->setBounds (0, y, w, juce::roundToInt (unitFor (w) * 2));
        y += chooseInstrument->getHeight() + 1;
    }
    for (auto* u : units) { const int h = u->heightFor (w); u->setBounds (0, y, w, h); y += h + 1; }
    addEffect->setBounds (0, y, w, juce::roundToInt (unitFor (w)));
    y += addEffect->getHeight() + 6;
    rack.setSize (w, juce::jmax (y, rackView.getHeight()));
}

void View::showInstrumentMenu()
{
    // the PEQUOD Keys (organ, electric pianos, clavinet, Mellotron, Minimoog, DX7) first, then the rest
    juce::PopupMenu m;
    const auto list = Engine::instruments();
    auto isKeys = [] (const PequodNative::Info* i) { return i->vendor.endsWithIgnoreCase ("Keys"); };
    for (int pass = 0; pass < 2; ++pass)
    {
        bool header = false;
        for (size_t k = 0; k < list.size(); ++k)
        {
            if (isKeys (list[k]) != (pass == 0)) continue;
            if (! header) { m.addSectionHeader (pass == 0 ? "KEYS" : "INSTRUMENTS"); header = true; }
            m.addItem ((int) k + 1, list[k]->name + "   " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  " + list[k]->vendor.fromLastOccurrenceOf ("PEQUOD", false, true).trim());
        }
    }
    if (list.empty()) m.addItem (-1, "(no PEQUOD instruments in this build)", false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rackView), [this, list] (int r)
    {
        if (r <= 0 || (size_t) r > list.size()) return;
        units.clear();
        engine.setInstrument (current, PequodNative::describe (*list[(size_t) r - 1]).fileOrIdentifier);
        rebuildRack();
    });
}

void View::showEffectMenu (int insertAt)
{
    // by collection, as in IRVING's browser
    const auto list = Engine::effects();
    std::map<juce::String, juce::PopupMenu> byVendor;
    for (size_t k = 0; k < list.size(); ++k)
    {
        auto v = list[k]->vendor.fromLastOccurrenceOf ("PEQUOD", false, true).trim();
        if (v.isEmpty()) v = "PEQUOD";
        byVendor[v].addItem ((int) k + 1, list[k]->name);
    }
    juce::PopupMenu m;
    for (auto& [v, sub] : byVendor) m.addSubMenu (v, sub);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rackView), [this, list, insertAt] (int r)
    {
        if (r <= 0 || (size_t) r > list.size()) return;
        units.clear();
        engine.addEffect (current, PequodNative::describe (*list[(size_t) r - 1]).fileOrIdentifier, insertAt);
        rebuildRack();
    });
}

void View::showMidiInputs()
{
    // ALCOTT's settings: which keyboards play it, and how long THOREAU's steps hold
    struct Panel : juce::Component
    {
        Panel (View& v) : owner (v)
        {
            for (int n : { 4, 8, 16, 32 }) length.addItem ("1/" + juce::String (n) + " note", n);
            length.setSelectedId (owner.engine.thoreauLength.load(), juce::dontSendNotification);
            length.onChange = [this] { owner.engine.thoreauLength = length.getSelectedId(); melville::setStepLength (length.getSelectedId()); };
            addAndMakeVisible (length);
            setSize (480, 200);
        }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (Pequod::Colours::bg);
            g.setFont (Pequod::heading (13.0f));
            g.setColour (accent());
            g.drawText ("MIDI", 18, 12, 300, 20, juce::Justification::centredLeft);
            g.drawText ("THOREAU", 18, getHeight() - 74, 300, 20, juce::Justification::centredLeft);
            g.setFont (Pequod::body (12.5f));
            g.setColour (Pequod::Colours::textDim);
            g.drawFittedText ("Each slot plays what's cabled into its MIDI IN on MELVILLE's PATCHBAY: KEYBOARDS (every MIDI input), "
                              "one keyboard on its own, THOREAU's steps, the CLOCK for arpeggiators.",
                              18, 34, getWidth() - 36, 50, juce::Justification::topLeft, 3);
            g.setFont (Pequod::body (13.0f));
            g.setColour (Pequod::Colours::text);
            g.drawText ("Its steps hold for a", 18, length.getY(), 160, length.getHeight(), juce::Justification::centredLeft);
        }
        void resized() override
        {
            length.setBounds (170, getHeight() - 46, 160, 26);
        }
        View& owner;
        juce::ComboBox length;
    };
    melville::showPanel (std::make_unique<Panel> (*this), "ALCOTT settings", "alcott");
}

juce::String View::statusLine() const
{
    int playing = 0;
    for (int i = 0; i < Engine::numSlots; ++i) if (const_cast<Engine&> (engine).slot (i).instrument != nullptr) ++playing;
    const auto dot = juce::String ("  ") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + "  ";
    return juce::String (playing) + (playing == 1 ? " INSTRUMENT" : " INSTRUMENTS") + dot
         + "TEMPO " + juce::String (melville::transport().bpm.load(), 1) + dot
         + juce::String (juce::roundToInt (engine.getSampleRate() / 100.0) / 10.0, 1) + " kHz";
}

void View::timerCallback()
{
    list->tick();
    static int ticks = 0;
    ++ticks;
    if (ticks % 10 == 0) repaint (statusArea);
}

void View::paint (juce::Graphics& g)
{
    g.fillAll (Pequod::Colours::bg);
    partui::drawBar (g, headerArea, accent());
    partui::drawWordmark (g, headerArea.reduced (18, 0).removeFromLeft (190), "ALCOTT", "INSTRUMENT RACK", accent(), Pequod::heading (30.0f));
    g.setFont (Pequod::heading (13.0f));
    g.setColour (accent());
    g.drawText (statusLine(), statusArea.withHeight (statusArea.getHeight() / 2).translated (0, 4), juce::Justification::centredLeft, true);
    g.setFont (Pequod::body (12.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawText ("PEQUOD instruments, played by whatever's cabled to their MIDI IN on MELVILLE's PATCHBAY. Their outs and the mix are there too",
                statusArea.withTrimmedTop (statusArea.getHeight() / 2).translated (0, -2), juce::Justification::centredLeft, true);

    // the list's heading, and the slot's
    g.setFont (Pequod::heading (12.0f));
    g.setColour (Pequod::Colours::textDim);
    g.drawText ("SLOTS", list->getBounds().withY (list->getY() - 24).withHeight (24).reduced (12, 0), juce::Justification::centredLeft);
    g.setFont (Pequod::heading (16.0f));
    g.setColour (accent());
    auto& s = const_cast<Engine&> (engine).slot (current);
    g.drawText ("SLOT " + juce::String (current + 1) + (s.shownName().isNotEmpty() ? "   " + s.shownName().toUpperCase() : juce::String()),
                controlsArea.withWidth (channel.getX() - controlsArea.getX() - 10), juce::Justification::centredLeft, true);
}

void View::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (partui::headerHeight);
    auto bar = headerArea.withTrimmedBottom (2).reduced (14, 0);
    bar.removeFromLeft (200);
    bar = partui::placeKeys (bar, { &midiKey });
    statusArea = bar.withTrimmedRight (10).withSizeKeepingCentre (bar.getWidth() - 10, 40);

    r.reduce (8, 8);
    auto left = r.removeFromLeft (320);
    left.removeFromTop (24);
    list->setBounds (left.withHeight (SlotList::rowH * Engine::numSlots));
    r.removeFromLeft (10);

    controlsArea = r.removeFromTop (40);
    auto c = controlsArea.reduced (0, 6);
    clear.setBounds (c.removeFromRight (80));
    c.removeFromRight (6);
    mute.setBounds (c.removeFromRight (70));
    c.removeFromRight (10);
    level.setBounds (c.removeFromRight (230));
    c.removeFromRight (10);
    channel.setBounds (c.removeFromRight (190));
    r.removeFromTop (4);
    rackView.setBounds (r);
    layoutRack();
}

} // namespace alcott
