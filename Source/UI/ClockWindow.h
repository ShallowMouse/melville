#pragma once

// MELVILLE's CLOCK: tempo, click and timecode for the whole studio (Hawthorne's SYNC and
// TIMECODE windows, moved here). And MTC out, from MELVILLE's clock.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "MelvilleLookAndFeel.h"
#include "../Core/Hub.h"
#include "MelvilleHosted.h"

namespace melville
{

//==============================================================================
/** MIDI Time Code out, following MELVILLE's clock (quarter frames while it rolls, a full
    frame when it stops or jumps), on a 1 ms timer. */
class MtcOut : private juce::HighResolutionTimer
{
public:
    explicit MtcOut (ClockNode& c) : clock (c) {}
    ~MtcOut() override { close(); }

    bool open (const juce::String& id)
    {
        close();
        if (id.isEmpty()) return true;
        auto out = juce::MidiOutput::openDevice (id);
        if (out == nullptr) return false;
        { const juce::ScopedLock sl (lock); output = std::move (out); }
        lastQ = -1; wasRolling = false; lastStill = -1.0;
        startTimer (1);
        return true;
    }

    void close()
    {
        stopTimer();
        const juce::ScopedLock sl (lock);
        output = nullptr;
    }

private:
    static int rateCode (int rate) { return rate == 0 ? 0 : rate == 1 ? 1 : rate == 2 ? 2 : 3; }   // 24, 25, 29.97 drop, 30 (29.97 non-drop sends 30)

    void hiResTimerCallback() override
    {
        const juce::ScopedLock sl (lock);
        if (output == nullptr || ! clock.getState) return;
        const auto s = clock.getState();
        if (! s.valid) return;
        const int rate = clock.tcRate.load();
        const auto r = (hawthorne::timecode::Rate) rate;
        // where the clock is now: the block's start plus the time since (good to a block)
        const double frames = clock.timecodeFramesAt (s.pos, s.clockRate);
        if (! s.rolling)
        {
            if (wasRolling || std::abs (frames - lastStill) > 0.5)
            {
                sendFull ((juce::int64) std::floor (frames), r, rate);
                lastStill = frames;
            }
            wasRolling = false;
            lastQ = -1;
            return;
        }
        wasRolling = true;
        const auto q = (juce::int64) std::floor (frames * 4.0);
        if (lastQ < 0 || q < lastQ || q - lastQ > 8) { sendFull ((juce::int64) std::floor (frames), r, rate); lastQ = q; return; }
        while (lastQ < q)
        {
            ++lastQ;
            const int piece = (int) (lastQ % 8);
            const auto base = hawthorne::timecode::fromFrames ((lastQ - piece) / 4, r);   // the frame the 8 pieces describe
            int v = 0;
            switch (piece)
            {
                case 0: v = base.frames & 15; break;
                case 1: v = base.frames >> 4; break;
                case 2: v = base.seconds & 15; break;
                case 3: v = base.seconds >> 4; break;
                case 4: v = base.minutes & 15; break;
                case 5: v = base.minutes >> 4; break;
                case 6: v = base.hours & 15; break;
                default: v = ((base.hours >> 4) & 1) | (rateCode (rate) << 1); break;
            }
            output->sendMessageNow (juce::MidiMessage (0xf1, (piece << 4) | v));
        }
    }

    void sendFull (juce::int64 frame, hawthorne::timecode::Rate r, int rate)
    {
        const auto t = hawthorne::timecode::fromFrames (juce::jmax ((juce::int64) 0, frame), r);
        const juce::uint8 data[] = { 0x7f, 0x7f, 0x01, 0x01, (juce::uint8) ((rateCode (rate) << 5) | t.hours),
                                     (juce::uint8) t.minutes, (juce::uint8) t.seconds, (juce::uint8) t.frames };
        output->sendMessageNow (juce::MidiMessage::createSysExMessage (data, (int) sizeof (data)));
    }

    ClockNode& clock;
    juce::CriticalSection lock;
    std::unique_ptr<juce::MidiOutput> output;
    juce::int64 lastQ = -1;
    bool wasRolling = false;
    double lastStill = -1.0;
};

//==============================================================================
class ClockPanel : public juce::Component, private juce::Timer
{
public:
    struct Settings { juce::String mtcOut, mtcIn; int chase = 0; };
    std::function<void (const Settings&)> onSettingsChanged;     // (saved by the app)

    ClockPanel (ClockNode& c, Settings s) : clock (c), settings (std::move (s))
    {
        auto label = [this] (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, Colours::textDim);
            l.setFont (juce::Font (juce::FontOptions (12.0f)));
            addAndMakeVisible (l);
        };

        // --- TEMPO
        label (tempoLabel, "Tempo");
        tempo.setRange (20.0, 400.0, 0.1);
        tempo.setSliderStyle (juce::Slider::LinearHorizontal);
        tempo.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        tempo.setTextValueSuffix (" BPM");
        tempo.onValueChange = [this] { if (! updating) { auto& t = transport(); setTempo (tempo.getValue(), t.beatsPerBar.load(), t.barOne.load()); } };
        addAndMakeVisible (tempo);
        label (meterLabel, "Meter");
        for (int b = 1; b <= 12; ++b) meter.addItem (juce::String (b) + "/4", b);
        meter.onChange = [this] { if (! updating) { auto& t = transport(); setTempo (t.bpm.load(), meter.getSelectedId(), t.barOne.load()); } };
        addAndMakeVisible (meter);
        tap.onClick = [this] { tapped(); };
        tap.setTooltip ("Tap the tempo (three taps or more)");
        barOneHere.onClick = [] { auto& t = transport(); setTempo (t.bpm.load(), t.beatsPerBar.load(), std::round ((double) t.position.load())); };
        barOneHere.setTooltip ("Bar 1 starts where the transport is now (stop it on the downbeat first, e.g. after a count-in)");
        barOneZero.onClick = [] { auto& t = transport(); setTempo (t.bpm.load(), t.beatsPerBar.load(), 0.0); };
        barOneZero.setTooltip ("Bar 1 at zero (the start of the tape)");
        for (auto* b : { &tap, &barOneHere, &barOneZero }) addAndMakeVisible (b);

        // --- CLICK
        clickOn.setClickingTogglesState (true);
        clickOn.onClick = [this] { clock.clickOn = clickOn.getToggleState(); };
        clickOn.setTooltip ("The click on its CLICK jack (MELVILLE panel), while the transport rolls");
        onlyRec.setClickingTogglesState (true);
        onlyRec.onClick = [this] { clock.clickOnlyRecording = onlyRec.getToggleState(); };
        onlyRec.setTooltip ("Click only while recording");
        accent.setClickingTogglesState (true);
        accent.onClick = [this] { clock.accentGain = accent.getToggleState() ? 1.0f : 0.7f; };
        accent.setTooltip ("The first beat of the bar louder (it's always higher)");
        for (auto* b : { &clickOn, &onlyRec, &accent }) addAndMakeVisible (b);
        label (clickLevelLabel, "Level");
        dbSlider (clickLevel, [this] (double db) { clock.clickGain = juce::Decibels::decibelsToGain ((float) db); });

        // --- TIMECODE
        label (rateLabel, "Rate");
        for (int r = 0; r < 5; ++r) rate.addItem (hawthorne::timecode::name ((hawthorne::timecode::Rate) r) + " fps", r + 1);
        rate.onChange = [this] { if (! updating) setTimecode (rate.getSelectedId() - 1, transport().tcStart.load()); };
        addAndMakeVisible (rate);
        label (startLabel, "Start");
        start.setTooltip ("The timecode at zero (hh:mm:ss:ff), e.g. 01:00:00:00");
        start.setColour (juce::TextEditor::backgroundColourId, Colours::panelHi);
        start.setColour (juce::TextEditor::outlineColourId, Colours::line);
        start.onReturnKey = start.onFocusLost = [this] { applyStart(); };
        addAndMakeVisible (start);
        ltcOn.setClickingTogglesState (true);
        ltcOn.onClick = [this] { clock.ltcOn = ltcOn.getToggleState(); };
        ltcOn.setTooltip ("LTC on its LTC OUT jack (MELVILLE panel) while the transport rolls");
        addAndMakeVisible (ltcOn);
        label (ltcLevelLabel, "LTC level");
        dbSlider (ltcLevel, [this] (double db) { clock.ltcGain = juce::Decibels::decibelsToGain ((float) db); });
        label (mtcOutLabel, "MTC out");
        mtcOut.onChange = [this] { if (! updating) { settings.mtcOut = idAt (mtcOut, outs); changed(); } };
        addAndMakeVisible (mtcOut);
        label (chaseLabel, "Chase");
        chase.addItem ("Off: MELVILLE runs the transport", 1);
        chase.addItem ("LTC on the LTC IN jack", 2);
        chase.addItem ("MTC from a MIDI input", 3);
        chase.onChange = [this] { if (! updating) { settings.chase = chase.getSelectedId() - 1; changed(); resized(); } };
        addAndMakeVisible (chase);
        mtcIn.onChange = [this] { if (! updating) { settings.mtcIn = idAt (mtcIn, ins); changed(); } };
        addChildComponent (mtcIn);

        refresh();
        setSize (600, 520);
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::bg);
        for (auto& [area, title] : { std::pair (tempoArea, "TEMPO"), std::pair (clickArea, "CLICK"), std::pair (tcArea, "TIMECODE") })
        {
            g.setColour (Colours::panel);
            g.fillRoundedRectangle (area.toFloat(), 6.0f);
            g.setColour (Colours::accent);
            g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
            g.drawText (title, area.reduced (14, 8).removeFromTop (18), juce::Justification::centredLeft);
        }
        g.setColour (Colours::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (status, tcArea.reduced (14, 8).removeFromBottom (18), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto rowOf = [] (juce::Rectangle<int>& a) { auto row = a.removeFromTop (28); a.removeFromTop (8); return row; };
        auto labelled = [] (juce::Rectangle<int>& row, juce::Label& l, int w) { l.setBounds (row.removeFromLeft (w)); };

        tempoArea = r.removeFromTop (128); r.removeFromTop (10);
        {
            auto a = tempoArea.reduced (14, 8).withTrimmedTop (26);
            auto row = rowOf (a);
            labelled (row, tempoLabel, 70); tap.setBounds (row.removeFromRight (70)); row.removeFromRight (8); tempo.setBounds (row);
            row = rowOf (a);
            labelled (row, meterLabel, 70); meter.setBounds (row.removeFromLeft (90)); row.removeFromLeft (16);
            barOneHere.setBounds (row.removeFromLeft (130)); row.removeFromLeft (8); barOneZero.setBounds (row.removeFromLeft (130));
        }
        clickArea = r.removeFromTop (128); r.removeFromTop (10);
        {
            auto a = clickArea.reduced (14, 8).withTrimmedTop (26);
            auto row = rowOf (a);
            clickOn.setBounds (row.removeFromLeft (110)); row.removeFromLeft (8);
            accent.setBounds (row.removeFromLeft (110)); row.removeFromLeft (8);
            onlyRec.setBounds (row.removeFromLeft (170));
            row = rowOf (a);
            labelled (row, clickLevelLabel, 70); clickLevel.setBounds (row);
        }
        tcArea = r;
        {
            auto a = tcArea.reduced (14, 8).withTrimmedTop (26);
            auto row = rowOf (a);
            labelled (row, rateLabel, 70); rate.setBounds (row.removeFromLeft (140)); row.removeFromLeft (16);
            labelled (row, startLabel, 46); start.setBounds (row.removeFromLeft (120));
            row = rowOf (a);
            ltcOn.setBounds (row.removeFromLeft (110)); row.removeFromLeft (16);
            labelled (row, ltcLevelLabel, 70); ltcLevel.setBounds (row);
            row = rowOf (a);
            labelled (row, mtcOutLabel, 70); mtcOut.setBounds (row);
            row = rowOf (a);
            labelled (row, chaseLabel, 70);
            chase.setBounds (row.removeFromLeft (260)); row.removeFromLeft (8);
            mtcIn.setVisible (settings.chase == 2);
            mtcIn.setBounds (row);
        }
    }

    void refresh()
    {
        const juce::ScopedValueSetter<bool> u (updating, true);
        auto& t = transport();
        if (! tempo.isMouseButtonDown()) tempo.setValue (t.bpm.load(), juce::dontSendNotification);
        meter.setSelectedId (t.beatsPerBar.load(), juce::dontSendNotification);
        clickOn.setToggleState (clock.clickOn.load(), juce::dontSendNotification);
        onlyRec.setToggleState (clock.clickOnlyRecording.load(), juce::dontSendNotification);
        accent.setToggleState (clock.accentGain.load() > 0.85f, juce::dontSendNotification);
        if (! clickLevel.isMouseButtonDown()) clickLevel.setValue (juce::Decibels::gainToDecibels (clock.clickGain.load()), juce::dontSendNotification);
        ltcOn.setToggleState (clock.ltcOn.load(), juce::dontSendNotification);
        if (! ltcLevel.isMouseButtonDown()) ltcLevel.setValue (juce::Decibels::gainToDecibels (clock.ltcGain.load()), juce::dontSendNotification);
        rate.setSelectedId (t.tcRate.load() + 1, juce::dontSendNotification);
        if (! start.hasKeyboardFocus (true))
        {
            const auto r = (hawthorne::timecode::Rate) t.tcRate.load();
            start.setText (hawthorne::timecode::fromFrames ((juce::int64) std::round (t.tcStart.load() * hawthorne::timecode::fps (r)), r)
                               .toString (hawthorne::timecode::isDrop (r)), false);
        }
        fillMidi (mtcOut, outs, juce::MidiOutput::getAvailableDevices(), "No MTC out", settings.mtcOut);
        fillMidi (mtcIn, ins, juce::MidiInput::getAvailableDevices(), "(choose a MIDI input)", settings.mtcIn);
        chase.setSelectedId (settings.chase + 1, juce::dontSendNotification);
    }

private:
    void timerCallback() override
    {
        refresh();
        // where the clock is, as timecode
        if (clock.getState)
        {
            const auto s = clock.getState();
            const auto r = (hawthorne::timecode::Rate) transport().tcRate.load();
            const auto now = hawthorne::timecode::fromFrames ((juce::int64) std::floor (clock.timecodeFramesAt (s.pos, s.clockRate)), r);
            const auto text = "Now " + now.toString (hawthorne::timecode::isDrop (r)) + (s.rolling ? "   rolling" : "   stopped");
            if (text != status) { status = text; repaint (tcArea); }
        }
    }

    void dbSlider (juce::Slider& s, std::function<void (double)> set)
    {
        s.setRange (-40.0, 0.0, 0.5);
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        s.setTextValueSuffix (" dB");
        s.onValueChange = [this, &s, set] { if (! updating) set (s.getValue()); };
        addAndMakeVisible (s);
    }

    template <typename List>
    void fillMidi (juce::ComboBox& box, juce::Array<juce::MidiDeviceInfo>& keep, const List& devices, const juce::String& none, const juce::String& selected)
    {
        if (box.isPopupActive()) return;
        if (keep == devices && box.getNumItems() > 0) { box.setSelectedId (selectedId (keep, selected), juce::dontSendNotification); return; }
        keep = devices;
        box.clear (juce::dontSendNotification);
        box.addItem (none, 1);
        for (int i = 0; i < keep.size(); ++i) box.addItem (keep[i].name, i + 2);
        box.setSelectedId (selectedId (keep, selected), juce::dontSendNotification);
    }
    static int selectedId (const juce::Array<juce::MidiDeviceInfo>& list, const juce::String& id)
    {
        for (int i = 0; i < list.size(); ++i) if (list[i].identifier == id) return i + 2;
        return 1;
    }
    static juce::String idAt (const juce::ComboBox& box, const juce::Array<juce::MidiDeviceInfo>& list)
    {
        const int i = box.getSelectedId() - 2;
        return juce::isPositiveAndBelow (i, list.size()) ? list[i].identifier : juce::String();
    }

    void applyStart()
    {
        const auto r = (hawthorne::timecode::Rate) transport().tcRate.load();
        auto parts = juce::StringArray::fromTokens (start.getText(), ":;.", {});
        while (parts.size() < 4) parts.insert (0, "0");
        hawthorne::timecode::Time tm;
        tm.hours = juce::jlimit (0, 23, parts[0].getIntValue());
        tm.minutes = juce::jlimit (0, 59, parts[1].getIntValue());
        tm.seconds = juce::jlimit (0, 59, parts[2].getIntValue());
        tm.frames = juce::jlimit (0, hawthorne::timecode::nominal (r) - 1, parts[3].getIntValue());
        setTimecode ((int) r, (double) hawthorne::timecode::toFrames (tm, r) / hawthorne::timecode::fps (r));
        refresh();
    }

    void tapped()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0) taps.clear();
        taps.push_back (now);
        if (taps.size() > 6) taps.erase (taps.begin());
        if (taps.size() >= 3)
        {
            auto& t = transport();
            setTempo (std::round (60000.0 / ((taps.back() - taps.front()) / (double) (taps.size() - 1)) * 10.0) / 10.0,
                      t.beatsPerBar.load(), t.barOne.load());
        }
    }

    void changed() { if (onSettingsChanged) onSettingsChanged (settings); }

    ClockNode& clock;
    Settings settings;
    bool updating = false;
    juce::String status;
    std::vector<double> taps;
    juce::Array<juce::MidiDeviceInfo> outs, ins;
    juce::Rectangle<int> tempoArea, clickArea, tcArea;

    juce::Label tempoLabel, meterLabel, clickLevelLabel, rateLabel, startLabel, ltcLevelLabel, mtcOutLabel, chaseLabel;
    juce::Slider tempo, clickLevel, ltcLevel;
    juce::ComboBox meter, rate, mtcOut, chase, mtcIn;
    juce::TextEditor start;
    juce::TextButton tap { "TAP" }, barOneHere { "BAR 1 HERE" }, barOneZero { "BAR 1 AT ZERO" },
                     clickOn { "CLICK ON" }, onlyRec { "ONLY RECORDING" }, accent { "ACCENT" }, ltcOn { "LTC OUT" };
};

} // namespace melville
