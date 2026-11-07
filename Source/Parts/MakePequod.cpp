// IRVING (was PEQUOD Host) inside MELVILLE (compiled with PEQUOD Host's settings).
#include "Parts.h"
#include "MainComponent.h"
#include "PequodLookAndFeel.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makePequod()
{
    auto ui = std::make_unique<PartUI>();
    ui->id = "pequod";
    ui->title = "IRVING";
    ui->background = Pequod::Colours::bg;       // (this part's own PEQUOD colours)
    ui->minWidth = 1300;
    ui->minHeight = 700;
    ui->fullScreen = true;
    // its own look, set on its window (not as everyone's default)
    ui->lookAndFeel = std::make_unique<PequodLookAndFeel>();
    ui->content = std::make_unique<::MainComponent>();
    return ui;
}

} // namespace melville::parts
