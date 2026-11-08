#include "Updater.h"

namespace melville
{

namespace
{
    juce::URL::InputStreamOptions options (int timeoutMs)
    {
        return juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                 .withExtraHeaders ("User-Agent: MELVILLE\r\nAccept: application/vnd.github+json\r\n")
                 .withConnectionTimeoutMs (timeoutMs)
                 .withNumRedirectsToFollow (8);
    }
}

Updater::Updater() : Thread ("MELVILLE updates") {}

Updater::~Updater()
{
    cancelPendingUpdate();
    stopThread (4000);
}

juce::String Updater::currentVersion()
{
    return MELVILLE_VERSION_STRING;
}

int Updater::compareVersions (const juce::String& a, const juce::String& b)
{
    auto pa = juce::StringArray::fromTokens (a.trimCharactersAtStart ("vV"), ".", {});
    auto pb = juce::StringArray::fromTokens (b.trimCharactersAtStart ("vV"), ".", {});
    for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
    {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

void Updater::checkSoon()
{
    if (available) return;              // found already
    if (isThreadRunning()) return;
    job = Job::check;
    startThread();
}

void Updater::install()
{
    if (! available || downloading || isThreadRunning()) return;
    job = Job::download;
    downloading = true;
    progress = 0.0f;
    if (onChange) onChange();
    startThread();
}

void Updater::run()
{
    if (job == Job::check)
    {
        // let MELVILLE finish starting first
        for (int i = 0; i < 40 && ! threadShouldExit(); ++i) wait (100);
        if (! threadShouldExit()) doCheck();
    }
    else if (job == Job::download)
        doDownload();
}

namespace
{
    /** First line of a release's notes, without developer lines. */
    juce::String changeLine (const juce::String& body)
    {
        for (auto line : juce::StringArray::fromLines (body))
        {
            line = line.trim().trimCharactersAtStart ("-*# ").trim();
            if (line.isEmpty() || line.startsWithIgnoreCase ("Co-Authored") || line.startsWithIgnoreCase ("Claude-"))
                continue;
            return line;
        }
        return {};
    }
}

/** One line per version newer than the installed one, newest first. */
juce::String Updater::changesSince (const juce::String& installed, const juce::String& latest, const juce::String& latestBody)
{
    juce::StringArray lines;
    juce::URL url ("https://api.github.com/repos/" + juce::String (releasesRepo) + "/releases?per_page=30");
    if (auto stream = url.createInputStream (options (8000)))
    {
        auto list = juce::JSON::parse (stream->readEntireStreamAsString());
        if (auto* arr = list.getArray())
            for (auto& r : *arr)
            {
                const auto v = r["tag_name"].toString().trimCharactersAtStart ("vV");
                if (compareVersions (v, installed) <= 0 || compareVersions (v, latest) > 0) continue;
                auto line = changeLine (r["body"].toString());
                if (line.isNotEmpty()) lines.addIfNotAlreadyThere (line);
            }
    }
    if (lines.isEmpty())
    {
        auto line = changeLine (latestBody);
        if (line.isNotEmpty()) lines.add (line);
    }
    const int maxLines = 10;
    juce::String out;
    for (int i = 0; i < juce::jmin (maxLines, lines.size()); ++i)
        out << juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2  ")) << lines[i] << "\n";
    if (lines.size() > maxLines) out << "...and " << (lines.size() - maxLines) << " more\n";
    return out.trimEnd();
}

void Updater::doCheck()
{
    juce::URL url ("https://api.github.com/repos/" + juce::String (releasesRepo) + "/releases/latest");
    auto stream = url.createInputStream (options (8000));
    if (stream == nullptr) return;                     // offline: try again next time
    auto json = juce::JSON::parse (stream->readEntireStreamAsString());
    if (! json.isObject()) return;

    const auto version = json["tag_name"].toString().trimCharactersAtStart ("vV");
    if (version.isEmpty() || compareVersions (version, currentVersion()) <= 0) return;

    juce::String foundUrl, foundName;
    if (auto* assets = json["assets"].getArray())
        for (auto& a : *assets)
        {
            auto name = a["name"].toString();
            if (name.startsWithIgnoreCase ("MELVILLE-Setup") && name.endsWithIgnoreCase (".exe"))
            {
                foundUrl = a["browser_download_url"].toString();
                foundName = name;
            }
        }
    if (foundUrl.isEmpty()) return;

    {
        const juce::ScopedLock sl (lock);
        latestVersion = version;
        notes = changesSince (currentVersion(), version, json["body"].toString());
        installerUrl = foundUrl;
        installerName = foundName;
    }
    available = true;
    triggerAsyncUpdate();
}

void Updater::doDownload()
{
    juce::String url, name;
    {
        const juce::ScopedLock sl (lock);
        url = installerUrl;
        name = installerName;
    }

    // A fresh file each try (a leftover installer from before may still be open), up to three tries,
    // and the reason kept if it fails
    juce::String reason;
    juce::File target;
    bool ok = false;
    for (int attempt = 1; attempt <= 3 && ! ok && ! threadShouldExit(); ++attempt)
    {
        target = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile (name.upToLastOccurrenceOf (".", false, false) + "-" + juce::String (juce::Time::currentTimeMillis()) + ".exe");
        int status = 0;
        auto in = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withExtraHeaders ("User-Agent: MELVILLE\r\nAccept: application/octet-stream\r\n")
                                                         .withConnectionTimeoutMs (30000)
                                                         .withNumRedirectsToFollow (10)
                                                         .withStatusCode (&status));
        if (in == nullptr || (status != 0 && status != 200))
        {
            reason = in == nullptr ? juce::String ("no connection to GitHub") : "GitHub answered " + juce::String (status);
            wait (1500 * attempt);
            continue;
        }
        const auto total = in->getTotalLength();
        int64_t done = 0;
        {
            juce::FileOutputStream out (target);
            if (! out.openedOk()) { reason = "couldn't write to " + target.getParentDirectory().getFullPathName(); continue; }
            out.truncate();
            juce::HeapBlock<char> buf (65536);
            while (! threadShouldExit())
            {
                const int n = in->read (buf, 65536);
                if (n <= 0) break;
                out.write (buf, (size_t) n);
                done += n;
                if (total > 0) { progress = (float) ((double) done / (double) total); triggerAsyncUpdate(); }
            }
            out.flush();
        }
        ok = ! threadShouldExit() && done > 100000 && (total <= 0 || done == total);
        if (! ok)
        {
            reason = "it stopped part-way (" + juce::String (done / 1024) + " of " + juce::String (juce::jmax ((juce::int64) 0, (juce::int64) total) / 1024) + " KB)";
            target.deleteFile();
            wait (1500 * attempt);
        }
    }

    {
        const juce::ScopedLock sl (lock);
        downloaded = ok ? target : juce::File();
        error = ok ? juce::String()
                   : "The update couldn't be downloaded (" + reason + "). Opening it in your browser instead: run the installer when it's downloaded.";
        if (! ok && ! threadShouldExit()) openInBrowser = url;
    }
    downloading = false;
    triggerAsyncUpdate();
}

void Updater::handleAsyncUpdate()
{
    juce::File toRun;
    juce::String err;
    {
        const juce::ScopedLock sl (lock);
        toRun = downloaded;
        downloaded = juce::File();
        err = error;
        error.clear();
    }

    juce::String browse;
    {
        const juce::ScopedLock sl (lock);
        std::swap (browse, openInBrowser);
    }
    if (onChange) onChange();
    if (err.isNotEmpty() && onStatus) onStatus (err);
    if (browse.isNotEmpty()) juce::URL (browse).launchInDefaultBrowser();    // (the download still happens: by the browser)

    if (toRun.existsAsFile())
    {
        // The installer closes MELVILLE if it's still open, replaces it and starts it again.
        if (toRun.startAsProcess ("/SILENT /SP- /SUPPRESSMSGBOXES /CLOSEAPPLICATIONS"))
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        else if (onStatus)
            onStatus ("The installer couldn't be started. It was saved to " + toRun.getFullPathName());
    }
}

} // namespace melville
