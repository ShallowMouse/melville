// ALCOTT, MELVILLE's instrument rack (compiled with PEQUOD Host's settings: it plays the PEQUOD engine).
#include "Parts.h"
#include <MelvilleHosted.h>
#include "AlcottView.h"
#include "PequodLookAndFeel.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makeAlcott()
{
    auto ui = std::make_unique<PartUI>();
    ui->id = "alcott";
    ui->title = "ALCOTT";
    ui->background = Pequod::Colours::bg;
    ui->minWidth = 1100;
    ui->minHeight = 640;
    ui->fullScreen = false;
    ui->lookAndFeel = std::make_unique<PequodLookAndFeel>();
    ui->content = std::make_unique<alcott::View>();
    return ui;
}

} // namespace melville::parts
