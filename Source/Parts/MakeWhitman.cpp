// WHITMAN inside MELVILLE (compiled with WHITMAN's settings).
#include "Parts.h"
#include "MainComponent.h"
#include "WhitmanLookAndFeel.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makeWhitman()
{
    auto ui = std::make_unique<PartUI>();
    ui->id = "whitman";
    ui->title = "WHITMAN";
    ui->background = Pequod::Colours::bg;       // (this part's own PEQUOD colours)
    ui->minWidth = 960;
    ui->minHeight = 780;
    ui->fullScreen = false;
    // its own look, set on its window (not as everyone's default)
    ui->lookAndFeel = std::make_unique<WhitmanLookAndFeel>();
    ui->content = std::make_unique<whitman::MainComponent>();
    return ui;
}

} // namespace melville::parts
