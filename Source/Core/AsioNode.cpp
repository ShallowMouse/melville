#include "AsioNode.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
 #include <shellapi.h>
#endif

namespace melville
{

namespace
{
    juce::StringArray numberedNames (const juce::String& prefix, int n)
    {
        juce::StringArray s;
        for (int i = 1; i <= n; ++i) s.add (prefix + juce::String (i));
        return s;
    }

    juce::int64 nowMs() noexcept { return (juce::int64) juce::Time::getMillisecondCounter(); }
}

AsioNode::AsioNode()
{
    port.id = "asio";
    port.title = "MELVILLE ASIO";
    port.sources = numberedNames ("Out ", asio::channels);    // what the program plays
    port.dests   = numberedNames ("In ", asio::channels);     // what it records
    open();
}

AsioNode::~AsioNode()
{
    if (shared != nullptr) shared->sampleRate = 0;
   #if JUCE_WINDOWS
    if (tick != nullptr) CloseHandle ((HANDLE) tick);
   #endif
}

void AsioNode::open()
{
    if (shared != nullptr) return;
    shared = static_cast<asio::Shared*> (memory.open (asio::mappingName, sizeof (asio::Shared)));
    if (shared != nullptr && shared->magic != asio::magic)
    {
        shared->magic = asio::magic;
        shared->version = asio::version;
    }
   #if JUCE_WINDOWS
    tick = CreateEventW (nullptr, FALSE, FALSE, asio::tickName);
   #endif
}

void AsioNode::prepare (double sampleRate, int)
{
    open();
    if (shared != nullptr) shared->sampleRate = (std::int32_t) std::lround (sampleRate);
    readPos = -1;
}

void AsioNode::release()
{
    if (shared != nullptr) shared->sampleRate = 0;        // (the driver keeps time by itself)
}

bool AsioNode::isConnected() const noexcept
{
    return shared != nullptr && shared->driverRunning.load (std::memory_order_relaxed) != 0;
}

juce::String AsioNode::clientName() const
{
    if (! isConnected()) return {};
    char name[sizeof (shared->clientName) + 1] {};
    std::memcpy (name, shared->clientName, sizeof (shared->clientName));
    return juce::String::fromUTF8 (name);
}

void AsioNode::pull (float* const* sources, int n) noexcept
{
    const auto clear = [&]
    {
        for (int ch = 0; ch < asio::channels; ++ch)
            if (sources[ch] != nullptr) juce::FloatVectorOperations::clear (sources[ch], n);
    };
    if (shared == nullptr || shared->driverRunning.load (std::memory_order_relaxed) == 0) { readPos = -1; clear(); return; }

    const auto w = shared->toWrite.load (std::memory_order_acquire);
    const auto client = shared->driverClientId.load (std::memory_order_relaxed);
    const int theirBlock = juce::jmax (16, (int) shared->driverBlock.load (std::memory_order_relaxed));
    const juce::int64 target = theirBlock + n + 32;
    // (re)lock: a new program, or drifted out of the window
    if (readPos < 0 || client != clientId || w - readPos > (juce::int64) asio::ringFrames / 2 || w - readPos < 0)
    {
        readPos = w - target;
        clientId = client;
    }
    latency = (int) target;
    for (int ch = 0; ch < asio::channels; ++ch)
    {
        float* d = sources[ch];
        if (d == nullptr) continue;
        const float* ring = shared->toMelville[ch];
        for (int k = 0; k < n; ++k)
        {
            const auto p = readPos + k;
            d[k] = p < w ? ring[p & asio::ringMask] : 0.0f;
        }
    }
    readPos += n;
}

void AsioNode::push (const float* const* dests, int n) noexcept
{
    if (shared == nullptr) return;
    const auto w = shared->fromWrite.load (std::memory_order_relaxed);
    for (int ch = 0; ch < asio::channels; ++ch)
    {
        float* ring = shared->fromMelville[ch];
        const float* s = dests[ch];
        for (int k = 0; k < n; ++k) ring[(w + k) & asio::ringMask] = s != nullptr ? s[k] : 0.0f;
    }
    shared->fromWrite.store (w + n, std::memory_order_release);
    shared->clock.store (shared->clock.load (std::memory_order_relaxed) + n, std::memory_order_release);
    shared->heartbeatMs.store (nowMs(), std::memory_order_relaxed);
   #if JUCE_WINDOWS
    if (tick != nullptr) SetEvent ((HANDLE) tick);
   #endif
}

//==============================================================================
// Installing the driver: like Hawthorne Route's. Each driver version has its own file name,
// so an update never overwrites a DLL that REAPER still has loaded - the registration just
// moves to the new one.
#if JUCE_WINDOWS
static const char* const asioClsidKey = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Classes\\CLSID\\{181AC03E-0ABE-4B6C-B9AF-C44ED44CB4C3}\\InprocServer32\\";
static juce::File appDir()           { return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory(); }
static juce::File versionedDriver()  { return appDir().getChildFile ("MelvilleAsio-v" + juce::String ((int) asio::version) + ".dll"); }
static juce::File registeredDriver() { return juce::File (juce::WindowsRegistry::getValue (asioClsidKey)); }

static juce::String runElevated (const juce::String& exe, const juce::String& params)
{
    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof (info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe.toWideCharPointer();
    info.lpParameters = params.toWideCharPointer();
    info.nShow = SW_HIDE;
    if (! ShellExecuteExW (&info) || info.hProcess == nullptr)
        return "Windows didn't allow it (the permission prompt was declined).";
    WaitForSingleObject (info.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess (info.hProcess, &code);
    CloseHandle (info.hProcess);
    return code == 0 ? juce::String() : "Registering the driver failed (code " + juce::String ((int) code) + ").";
}
#endif

bool AsioNode::isDriverInstalled()
{
   #if JUCE_WINDOWS
    return juce::WindowsRegistry::valueExists ("HKEY_LOCAL_MACHINE\\SOFTWARE\\ASIO\\MELVILLE ASIO\\CLSID");
   #else
    return false;
   #endif
}

bool AsioNode::isDriverCurrent()
{
   #if JUCE_WINDOWS
    const auto reg = registeredDriver();
    return reg == versionedDriver() && reg.existsAsFile();
   #else
    return true;
   #endif
}

juce::String AsioNode::installDriver()
{
   #if JUCE_WINDOWS
    const auto fresh = appDir().getChildFile ("MelvilleAsio.dll");
    const auto dll = versionedDriver();
    if (fresh.existsAsFile() && ! (dll.existsAsFile() && dll.hasIdenticalContentTo (fresh)))
        if (! fresh.copyFileTo (dll))
            return "Couldn't put the driver in place (" + dll.getFileName() + ").\n"
                   "Close any program using MELVILLE ASIO (REAPER...) and try again.";
    if (! dll.existsAsFile())
        return "The driver isn't included in this build of MELVILLE.";
    const auto err = runElevated ("regsvr32.exe", "/s \"" + dll.getFullPathName() + "\"");
    if (err.isEmpty())
        for (auto& f : appDir().findChildFiles (juce::File::findFiles, false, "MelvilleAsio-v*.dll"))
            if (f != dll) f.deleteFile();       // (older ones; any still loaded stay until that program closes)
    return err;
   #else
    return "MELVILLE ASIO is for Windows.";
   #endif
}

juce::String AsioNode::uninstallDriver()
{
   #if JUCE_WINDOWS
    auto dll = registeredDriver();
    if (! dll.existsAsFile()) dll = versionedDriver();
    return runElevated ("regsvr32.exe", "/s /u \"" + dll.getFullPathName() + "\"");
   #else
    return {};
   #endif
}

} // namespace melville
