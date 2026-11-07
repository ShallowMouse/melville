// IRVING (was PEQUOD Host) inside MELVILLE (compiled with PEQUOD Host's settings).
#include "Parts.h"
#include <MelvilleHosted.h>
#include "MainComponent.h"
#include "PequodLookAndFeel.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makePequod (int instance)
{
    auto ui = std::make_unique<PartUI>();
    ui->id = instance <= 1 ? juce::String ("pequod") : "pequod" + juce::String (instance);
    ui->title = instance <= 1 ? juce::String ("IRVING") : "IRVING " + juce::String (instance);
    ui->background = Pequod::Colours::bg;       // (this part's own PEQUOD colours)
    ui->minWidth = 1300;
    ui->minHeight = 700;
    ui->fullScreen = true;
    // its own look, set on its window (not as everyone's default)
    ui->lookAndFeel = std::make_unique<PequodLookAndFeel>();
    melville::setNextPart (ui->id);              // this IRVING opens its own part's device
    ui->content = std::make_unique<::MainComponent>();
    melville::setNextPart ({});
    return ui;
}

} // namespace melville::parts
