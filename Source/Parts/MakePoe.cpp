// POE, MELVILLE's step sequencer (compiled with PEQUOD Host's settings, for its look and hardware drawing).
#include "Parts.h"
#include <MelvilleHosted.h>
#include "PoeView.h"
#include "PequodLookAndFeel.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makePoe()
{
    auto ui = std::make_unique<PartUI>();
    ui->id = "poe";
    ui->title = "POE";
    ui->background = Pequod::Colours::bg;
    ui->minWidth = 1240;
    ui->minHeight = 640;
    ui->fullScreen = false;
    ui->lookAndFeel = std::make_unique<poe::LookAndFeel>();
    ui->content = std::make_unique<poe::View>();
    return ui;
}

} // namespace melville::parts
