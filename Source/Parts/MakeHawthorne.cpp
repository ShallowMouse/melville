// Hawthorne inside MELVILLE (compiled with Hawthorne's settings).
#include "Parts.h"
#include "UI/MainComponent.h"

namespace melville::parts
{

std::unique_ptr<PartUI> makeHawthorne()
{
    // Hawthorne's own settings file, so MELVILLE's Hawthorne has the same machine, reels and library
    auto props = std::make_shared<juce::ApplicationProperties>();
    juce::PropertiesFile::Options o;
    o.applicationName     = "Hawthorne";
    o.filenameSuffix      = ".settings";
    o.folderName          = "Hawthorne";
    o.osxLibrarySubFolder = "Application Support";
    props->setStorageParameters (o);

    auto ui = std::make_unique<PartUI>();
    ui->id = "hawthorne";
    ui->title = "Hawthorne";
    ui->background = hawthorne::Colours::bg;
    ui->minWidth = 1340;
    ui->minHeight = 600;
    ui->nativeTitleBar = false;          // Hawthorne draws its own (its close button hides it in MELVILLE)
    ui->fullScreen = true;
    auto* mc = new hawthorne::MainComponent (*props);
    ui->content.reset (mc);
    ui->shutdown = [mc, props] { mc->shutdown(); props->saveIfNeeded(); };
    ui->keep = props;
    return ui;
}

} // namespace melville::parts
