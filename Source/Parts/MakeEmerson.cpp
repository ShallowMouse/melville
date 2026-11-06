// EMERSON inside MELVILLE (compiled with EMERSON's settings).
#include "Parts.h"
#include "MainComponent.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makeEmerson()
{
    auto ui = std::make_unique<PartUI>();
    ui->id = "emerson";
    ui->title = "EMERSON";
    ui->background = Pequod::Colours::bg;       // (this part's own PEQUOD colours)
    ui->minWidth = 1100;
    ui->minHeight = 700;
    ui->fullScreen = false;
    // its own look, set on its window (not as everyone's default)
    ui->lookAndFeel = std::make_unique<EmersonLookAndFeel>();
    ui->content = std::make_unique<emerson::MainComponent>();
    return ui;
}

} // namespace melville::parts
