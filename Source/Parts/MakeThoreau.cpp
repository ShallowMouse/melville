// FULLER inside MELVILLE (compiled with FULLER's settings).
#include "Parts.h"
#include "MainComponent.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makeThoreau()
{
    auto ui = std::make_unique<PartUI>();
    ui->id = "thoreau";
    ui->title = "FULLER";
    ui->background = Pequod::Colours::bg;       // (this part's own PEQUOD colours)
    ui->minWidth = 1300;
    ui->minHeight = 780;
    ui->fullScreen = false;
    // its own look, set on its window (not as everyone's default)
    ui->lookAndFeel = std::make_unique<ThoreauLookAndFeel>();
    ui->content = std::make_unique<thoreau::MainComponent>();
    return ui;
}

} // namespace melville::parts
