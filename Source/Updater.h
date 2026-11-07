#pragma once

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace melville
{

//==============================================================================
/** MELVILLE's updates: checks ShallowMouse/Melville-Releases for a newer MELVILLE (a few
    seconds after it starts, then every half hour) and installs it: downloads
    MELVILLE-Setup-x.y.z.exe and runs it (it closes MELVILLE, installs, starts it again).
    The parts inside MELVILLE come with it, so their own updaters stay off here. */
class Updater : private juce::Thread,
                private juce::AsyncUpdater
{
public:
    static constexpr const char* releasesRepo = "ShallowMouse/Melville-Releases";

    Updater();
    ~Updater() override;

    void checkSoon();                 // in the background, a few seconds after start

    bool isUpdateAvailable() const    { return available; }
    juce::String getLatestVersion() const { return latestVersion; }
    juce::String getReleaseNotes() const  { return notes; }
    bool isDownloading() const        { return downloading; }
    float getProgress() const         { return progress; }

    /** Downloads the installer and runs it; MELVILLE quits so it can be replaced. */
    void install();

    std::function<void()> onChange;                        // availability / progress changed (message thread)
    std::function<void (const juce::String&)> onStatus;

    static juce::String currentVersion();
    static juce::String changesSince (const juce::String& installed, const juce::String& latest, const juce::String& latestBody);
    static int compareVersions (const juce::String& a, const juce::String& b);   // <0 a older, 0 same, >0 a newer

private:
    std::atomic<bool> available { false }, downloading { false };
    std::atomic<float> progress { 0.0f };
    juce::String latestVersion, notes, installerUrl, installerName, error;
    juce::CriticalSection lock;
    enum class Job { none, check, download } job = Job::none;
    juce::File downloaded;

    void run() override;
    void handleAsyncUpdate() override;
    void doCheck();
    void doDownload();
};

} // namespace melville
