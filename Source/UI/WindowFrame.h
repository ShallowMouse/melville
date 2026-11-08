#pragma once

// MELVILLE's own window bar, in place of the system's on every window it opens (the parts,
// the patchbay, MELVILLE LINK, INTERFACE, the bar itself): the part's icon and name in its
// colour, then minimise / maximise / close keys drawn like the bar's icons.

#include <juce_gui_basics/juce_gui_basics.h>
#include "MelvilleLookAndFeel.h"

namespace melville
{

class WindowTitleBar : public juce::Component
{
public:
    static constexpr int height = 30;
    enum Buttons { minimise = 1, maximise = 2, close = 4, all = 7 };

    WindowTitleBar (juce::DocumentWindow& w, const juce::String& t, juce::Colour a, const juce::Image& i, int which)
        : window (w), title (t.toUpperCase()), accent (a), icon (i)
    {
        for (int b : { minimise, maximise, close })
            if ((which & b) != 0) addAndMakeVisible (keys.add (new Key (*this, b)));
        setRepaintsOnMouseActivity (false);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (juce::Colour (0xff111314));
        g.fillRect (r);
        g.setColour (accent.withAlpha (0.55f));
        g.fillRect (r.removeFromBottom (1));

        r.removeFromLeft (10);
        if (icon.isValid())
        {
            g.drawImageWithin (icon, r.getX(), r.getCentreY() - 9, 18, 18, juce::RectanglePlacement::centred);
            r.removeFromLeft (26);
        }
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (accent);
        const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 2;
        g.drawText (title, r.removeFromLeft (tw), juce::Justification::centredLeft, false);
        if (title != "MELVILLE")
        {
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.setColour (Colours::textDim);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   MELVILLE")), r, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedBottom (1);
        for (int i = keys.size(); --i >= 0;)
            keys[i]->setBounds (r.removeFromRight (44));
    }

    // drag the window by its bar; double-click maximises
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = ! e.mods.isPopupMenu();
        if (dragging && ! window.isFullScreen()) dragger.startDraggingComponent (&window, e.getEventRelativeTo (&window));
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || e.getDistanceFromDragStart() < 4) return;
        if (window.isFullScreen())
        {
            // dragging a maximised window: restore it under the mouse, then carry on
            const auto screen = e.getScreenPosition();
            const float fx = (float) e.getMouseDownX() / (float) juce::jmax (1, getWidth());
            window.setFullScreen (false);
            auto b = window.getBounds();
            if (b.getWidth() >= window.getParentMonitorArea().getWidth() - 20)
                b = b.withSizeKeepingCentre (b.getWidth() * 3 / 4, b.getHeight() * 3 / 4);
            b.setPosition (screen.x - (int) (fx * (float) b.getWidth()), screen.y - e.getMouseDownY());
            window.setBounds (b);
            dragger.startDraggingComponent (&window, e.getEventRelativeTo (&window));
            repaint();
        }
        dragger.dragComponent (&window, e.getEventRelativeTo (&window), nullptr);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (window.isResizable()) toggleMaximised();
    }

    void toggleMaximised() { window.setFullScreen (! window.isFullScreen()); repaint(); }

private:
    struct Key : juce::Button
    {
        Key (WindowTitleBar& o, int k) : juce::Button ({}), owner (o), kind (k)
        {
            setWantsKeyboardFocus (false);
            setTooltip (k == minimise ? "Minimise" : k == maximise ? "Maximise" : "Close");
            onClick = [this]
            {
                auto& w = owner.window;
                if (kind == minimise) w.setMinimised (true);
                else if (kind == maximise) owner.toggleMaximised();
                else w.closeButtonPressed();
            };
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            if (over || down)
            {
                g.setColour (kind == close ? Colours::warn.withAlpha (down ? 1.0f : 0.85f)
                                           : Colours::panelHi.brighter (down ? 0.15f : 0.0f));
                g.fillRect (r);
            }
            g.setColour (kind == close && over ? Colours::text : over ? owner.accent : Colours::textDim);
            const auto c = r.getCentre();
            const float s = 5.0f;
            juce::Path p;
            if (kind == minimise)
                p.addLineSegment ({ c.x - s, c.y + 0.5f, c.x + s, c.y + 0.5f }, 1.5f);
            else if (kind == maximise)
            {
                if (owner.window.isFullScreen())
                {
                    // restore: two squares, the front one over the back
                    const auto ink = over ? owner.accent : Colours::textDim;
                    const juce::Rectangle<float> back (c.x - s + 3.0f, c.y - s, 2 * s - 3.0f, 2 * s - 3.0f),
                                                 front (c.x - s, c.y - s + 3.0f, 2 * s - 3.0f, 2 * s - 3.0f);
                    g.drawRect (back, 1.5f);
                    g.setColour ((over || down) ? Colours::panelHi.brighter (down ? 0.15f : 0.0f) : juce::Colour (0xff111314));
                    g.fillRect (front);
                    g.setColour (ink);
                    g.drawRect (front, 1.5f);
                    return;
                }
                g.drawRect (juce::Rectangle<float> (c.x - s, c.y - s, 2 * s, 2 * s), 1.5f);
                return;
            }
            else
            {
                p.addLineSegment ({ c.x - s, c.y - s, c.x + s, c.y + s }, 1.5f);
                p.addLineSegment ({ c.x - s, c.y + s, c.x + s, c.y - s }, 1.5f);
            }
            g.fillPath (p);
        }

        WindowTitleBar& owner;
        int kind;
    };

    juce::DocumentWindow& window;
    juce::String title;
    juce::Colour accent;
    juce::Image icon;
    juce::OwnedArray<Key> keys;
    juce::ComponentDragger dragger;
    bool dragging = false;
};

/** A window's content under MELVILLE's bar. */
class FramedContent : public juce::Component
{
public:
    FramedContent (juce::DocumentWindow& w, juce::Component* c, bool owned, const juce::String& title,
                   juce::Colour accent, const juce::Image& icon, int buttons)
        : bar (w, title, accent, icon, buttons), content (c)
    {
        if (owned) ownedContent.reset (c);
        addAndMakeVisible (bar);
        addAndMakeVisible (content);
        setSize (content->getWidth(), content->getHeight() + WindowTitleBar::height);
        content->addComponentListener (&follower);
    }

    ~FramedContent() override
    {
        content->removeComponentListener (&follower);
        removeChildComponent (content);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        bar.setBounds (r.removeFromTop (WindowTitleBar::height));
        const juce::ScopedValueSetter<bool> s (laying, true);
        content->setBounds (r);
    }

    WindowTitleBar& getTitleBar() { return bar; }
    juce::Component* getContent() const { return content; }

private:
    // the content asking for a new size (a part changing its own) resizes the window
    struct Follower : juce::ComponentListener
    {
        FramedContent& o;
        explicit Follower (FramedContent& f) : o (f) {}
        void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
        {
            if (wasResized && ! o.laying)
                o.setSize (c.getWidth(), c.getHeight() + WindowTitleBar::height);
        }
    };

    WindowTitleBar bar;
    juce::Component* content;
    std::unique_ptr<juce::Component> ownedContent;
    bool laying = false;
    Follower follower { *this };
};

/** Puts MELVILLE's bar on a window in place of the system's. content: what the window shows
    (owned: the window deletes it). Content and window keep their sizes in step. */
inline FramedContent* frameWindow (juce::DocumentWindow& w, juce::Component* content, bool owned, const juce::String& title,
                                   juce::Colour accent, const juce::Image& icon, int buttons = WindowTitleBar::all)
{
    w.setUsingNativeTitleBar (false);
    w.setTitleBarHeight (0);
    w.setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff111314));   // (the thin border)
    auto* f = new FramedContent (w, content, owned, title, accent, icon, buttons);
    w.setContentOwned (f, true);
    return f;
}

} // namespace melville
