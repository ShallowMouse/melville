#include "PatchbayView.h"
#include "MelvilleLookAndFeel.h"

namespace melville
{

namespace
{
    // "Hawthorne track 40" on a 24-track reel: hidden
    bool beyondReel (const juce::String& name, int maxTracks)
    {
        const int i = name.indexOfIgnoreCase ("track ");
        if (i < 0) return false;
        return name.substring (i + 6).getIntValue() > maxTracks;
    }
}

//==============================================================================
PatchGrid::PatchGrid (Patchbay& p) : patchbay (p) {}

void PatchGrid::setPorts (int fromPort, int toPort, int maxTracks)
{
    sources.clear();
    dests.clear();
    const auto& ports = patchbay.getPorts();
    if (juce::isPositiveAndBelow (fromPort, (int) ports.size()))
    {
        const auto& p = ports[(size_t) fromPort];
        fromId = p.id;
        for (int i = 0; i < p.sources.size(); ++i)
            if (! beyondReel (p.sources[i], maxTracks)) sources.push_back (p.firstSource + i);
    }
    if (juce::isPositiveAndBelow (toPort, (int) ports.size()))
    {
        const auto& p = ports[(size_t) toPort];
        toId = p.id;
        for (int i = 0; i < p.dests.size(); ++i)
            if (! beyondReel (p.dests[i], maxTracks)) dests.push_back (p.firstDest + i);
    }
    setSize (labelWidth + juce::jmax (1, (int) sources.size()) * cell + 1,
             headerHeight + juce::jmax (1, (int) dests.size()) * cell + 1);
    repaint();
}

std::pair<int, int> PatchGrid::cellAt (juce::Point<int> p) const
{
    const int col = p.x >= labelWidth ? (p.x - labelWidth) / cell : -1;
    const int row = p.y >= headerHeight ? (p.y - headerHeight) / cell : -1;
    return { col < (int) sources.size() ? col : -1, row < (int) dests.size() ? row : -1 };
}

static juce::String channelOnly (const juce::String& full) { return full.fromFirstOccurrenceOf (" / ", false, false); }

void PatchGrid::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    const auto fromColour = Colours::forPart (fromId), toColour = Colours::forPart (toId);

    // column headers (the sources), written upwards
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    for (int c = 0; c < (int) sources.size(); ++c)
    {
        const int x = labelWidth + c * cell;
        if (c == hoverCol) { g.setColour (Colours::panelHi); g.fillRect (x, 0, cell, getHeight()); }
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) x + cell * 0.5f, (float) headerHeight - 4));
        g.setColour (c == hoverCol ? fromColour : Colours::text);
        g.drawText (channelOnly (patchbay.sourceName (sources[(size_t) c])),
                    juce::Rectangle<int> (x + cell / 2, headerHeight - 4 - cell / 2, headerHeight - 8, cell),
                    juce::Justification::centredLeft, true);
        g.restoreState();
    }

    // row labels (the destinations)
    for (int r = 0; r < (int) dests.size(); ++r)
    {
        const int y = headerHeight + r * cell;
        if (r == hoverRow) { g.setColour (Colours::panelHi); g.fillRect (0, y, getWidth(), cell); }
        g.setColour (r == hoverRow ? toColour : Colours::text);
        g.drawText (channelOnly (patchbay.destName (dests[(size_t) r])), 6, y, labelWidth - 10, cell, juce::Justification::centredRight, true);
    }

    // the grid
    g.setColour (Colours::line);
    for (int c = 0; c <= (int) sources.size(); ++c)
        g.drawVerticalLine (labelWidth + c * cell, (float) headerHeight, (float) (headerHeight + (int) dests.size() * cell));
    for (int r = 0; r <= (int) dests.size(); ++r)
        g.drawHorizontalLine (headerHeight + r * cell, (float) labelWidth, (float) (labelWidth + (int) sources.size() * cell));

    // cords
    for (auto& [d, cord] : patchbay.getAllCords())
    {
        const auto ci = std::find (sources.begin(), sources.end(), cord.source);
        const auto ri = std::find (dests.begin(), dests.end(), d);
        if (ci == sources.end() || ri == dests.end()) continue;
        const auto r = juce::Rectangle<int> (labelWidth + (int) (ci - sources.begin()) * cell,
                                             headerHeight + (int) (ri - dests.begin()) * cell, cell, cell).reduced (4).toFloat();
        g.setColour (Colours::accent.withAlpha (cord.gain < 0.999f ? 0.55f : 1.0f));
        g.fillEllipse (r);
    }
}

void PatchGrid::mouseMove (const juce::MouseEvent& e)
{
    auto [c, r] = cellAt (e.getPosition());
    if (c != hoverCol || r != hoverRow)
    {
        hoverCol = c; hoverRow = r;
        if (c >= 0 && r >= 0)
            setTooltip (patchbay.sourceName (sources[(size_t) c]) + "  ->  " + patchbay.destName (dests[(size_t) r]));
        else
            setTooltip ({});
        repaint();
    }
}

void PatchGrid::mouseExit (const juce::MouseEvent&)
{
    hoverCol = hoverRow = -1;
    repaint();
}

void PatchGrid::mouseDown (const juce::MouseEvent& e)
{
    auto [c, r] = cellAt (e.getPosition());
    if (c < 0 || r < 0) return;
    const int s = sources[(size_t) c], d = dests[(size_t) r];

    if (e.mods.isPopupMenu())
    {
        if (! patchbay.isConnected (s, d)) return;
        juce::PopupMenu m;
        m.addSectionHeader ("CORD LEVEL");
        const std::pair<const char*, float> levels[] = { { "0 dB", 1.0f }, { "-3 dB", 0.7079f }, { "-6 dB", 0.5012f },
                                                         { "-12 dB", 0.2512f }, { "-18 dB", 0.1259f } };
        int id = 1;
        for (auto& [label, gain] : levels) m.addItem (id++, label);
        m.addSeparator();
        m.addItem (99, "Remove cord");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                         [this, s, d, levels] (int chosen)
                         {
                             if (chosen == 99) patchbay.disconnect (s, d);
                             else if (chosen >= 1 && chosen <= 5) patchbay.connect (s, d, levels[chosen - 1].second);
                             repaint();
                         });
        return;
    }

    if (patchbay.isConnected (s, d)) patchbay.disconnect (s, d);
    else patchbay.connect (s, d);
    repaint();
}

//==============================================================================
PatchbayView::PatchbayView (Hub& h) : hub (h), grid (h.getPatchbay())
{
    for (auto* l : { &fromLabel, &toLabel })
    {
        l->setColour (juce::Label::textColourId, Colours::textDim);
        addAndMakeVisible (l);
    }

    const auto& ports = hub.getPatchbay().getPorts();
    int fromDefault = 1, toDefault = 1;
    for (int i = 0; i < (int) ports.size(); ++i)
    {
        if (! ports[(size_t) i].sources.isEmpty()) fromBox.addItem (ports[(size_t) i].title, i + 1);
        if (! ports[(size_t) i].dests.isEmpty())   toBox.addItem (ports[(size_t) i].title, i + 1);
        if (ports[(size_t) i].id == "interface") fromDefault = i + 1;
        if (ports[(size_t) i].id == "hawthorne") toDefault = i + 1;
    }
    fromBox.setSelectedId (fromDefault, juce::dontSendNotification);
    toBox.setSelectedId (toDefault, juce::dontSendNotification);
    fromBox.onChange = toBox.onChange = [this] { refresh(); };
    addAndMakeVisible (fromBox);
    addAndMakeVisible (toBox);

    defaultButton.setTooltip ("Put every cord back the way MELVILLE starts: interface -> Hawthorne -> EMERSON -> WHITMAN -> back to Hawthorne");
    defaultButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Default patch",
            "Replace every cord with MELVILLE's default patch?", "Replace", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int ok) { if (ok) { hub.defaultPatch(); grid.repaint(); } }));
    };
    clearButton.setTooltip ("Remove the cords shown (from this FROM to this TO)");
    clearButton.onClick = [this]
    {
        auto& pb = hub.getPatchbay();
        const auto& ps = pb.getPorts();
        const auto& from = ps[(size_t) (fromBox.getSelectedId() - 1)];
        const auto& to = ps[(size_t) (toBox.getSelectedId() - 1)];
        for (int d = 0; d < to.dests.size(); ++d)
            for (int s = 0; s < from.sources.size(); ++s)
                if (pb.isConnected (from.firstSource + s, to.firstDest + d))
                    pb.disconnect (from.firstSource + s, to.firstDest + d);
        grid.repaint();
    };
    saveButton.onClick = [this] { save(); };
    openButton.onClick = [this] { open(); };
    for (auto* b : { &defaultButton, &clearButton, &saveButton, &openButton }) addAndMakeVisible (b);

    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    hub.getPatchbay().onChange = [this] { grid.repaint(); };
    refresh();
    setSize (1000, 720);
}

PatchbayView::~PatchbayView()
{
    hub.getPatchbay().onChange = nullptr;
}

void PatchbayView::refresh()
{
    grid.setPorts (fromBox.getSelectedId() - 1, toBox.getSelectedId() - 1, hub.getVisibleTracks());
}

void PatchbayView::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    g.setColour (Colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (44));
}

void PatchbayView::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (44).reduced (10, 8);
    fromLabel.setBounds (bar.removeFromLeft (48));
    fromBox.setBounds (bar.removeFromLeft (190));
    bar.removeFromLeft (16);
    toLabel.setBounds (bar.removeFromLeft (30));
    toBox.setBounds (bar.removeFromLeft (190));
    for (auto* b : { &openButton, &saveButton, &clearButton, &defaultButton })
    {
        b->setBounds (bar.removeFromRight (b == &clearButton ? 104 : 80));
        bar.removeFromRight (6);
    }
    viewport.setBounds (r);
}

void PatchbayView::save()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.melpatch");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              hub.getPatchbay().toXml()->writeTo (f.withFileExtension ("melpatch"));
                          });
}

void PatchbayView::open()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a patch", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.melpatch");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (auto xml = juce::parseXML (fc.getResult()))
                                  if (xml->hasTagName ("PATCH")) { hub.getPatchbay().fromXml (*xml); grid.repaint(); }
                          });
}

} // namespace melville
