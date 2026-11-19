// MELVILLE ASIO - a virtual ASIO driver, like ReaRoute is to REAPER.
//
// Pick "MELVILLE ASIO" as the audio device in another program (REAPER, Guitar Rig...):
//   its outputs 1-24 -> MELVILLE's patchbay, the MELVILLE ASIO panel's OUT jacks
//   its inputs  1-24 <- whatever is patched to that panel's IN jacks
// Clocked by MELVILLE's own audio device (see Source/Asio/MelvilleAsioShared.h), so the
// two run sample-synchronously. Without MELVILLE running it keeps time by itself.
//
// A plain Win32 COM in-process server (no JUCE). Registered machine-wide (ASIO hosts look
// in HKLM\SOFTWARE\ASIO), which MELVILLE does once with an elevated regsvr32.
//
// Built against Steinberg's ASIO SDK headers (the copy bundled with JUCE), under the SDK's
// GPLv3 option: this driver's source is GPLv3 (see LICENSE.txt beside it).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <olectl.h>
#include <avrt.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"

#include "../Asio/MelvilleAsioShared.h"

namespace
{
    // {181AC03E-0ABE-4B6C-B9AF-C44ED44CB4C3}
    const CLSID CLSID_MelvilleAsio = { 0x181ac03e, 0x0abe, 0x4b6c, { 0xb9, 0xaf, 0xc4, 0x4e, 0xd4, 0x4c, 0xb4, 0xc3 } };
    const char* const clsidString = "{181AC03E-0ABE-4B6C-B9AF-C44ED44CB4C3}";
    const char* const driverName  = melville::asio::driverName;

    HMODULE thisModule = nullptr;
    std::atomic<long> objectCount { 0 }, lockCount { 0 };

    namespace route = melville::asio;      // (the same shape as Hawthorne Route's driver)

    long long nowMs() { return (long long) GetTickCount64(); }

    //==========================================================================
    class MelvilleAsioDriver : public IASIO
    {
    public:
        MelvilleAsioDriver()  { ++objectCount; }
        ~MelvilleAsioDriver() { stopThread(); disposeBuffers(); closeShared(); --objectCount; }

        // IUnknown (ASIO hosts ask for the driver's CLSID as the interface)
        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
        {
            if (ppv == nullptr) return E_POINTER;
            if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, CLSID_MelvilleAsio))
            {
                *ppv = static_cast<IASIO*> (this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override  { return (ULONG) ++refs; }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const long r = --refs;
            if (r == 0) delete this;
            return (ULONG) r;
        }

        // IASIO
        ASIOBool init (void*) override
        {
            openShared();
            std::strcpy (error, "");
            return ASIOTrue;
        }
        void getDriverName (char* name) override       { std::strcpy (name, driverName); }
        long getDriverVersion() override               { return 1; }
        void getErrorMessage (char* s) override        { std::strcpy (s, error); }

        ASIOError start() override
        {
            if (! buffersReady) return ASE_NotPresent;
            if (running) return ASE_OK;
            running = true;
            switchCount = 0;
            if (shared != nullptr)
            {
                shared->driverBlock = block;
                // the program's name, for MELVILLE's patchbay ("REAPER", "Guitar Rig 7"...)
                char exe[MAX_PATH] {};
                GetModuleFileNameA (nullptr, exe, MAX_PATH);
                std::string nm (exe);
                if (const auto slash = nm.find_last_of ("\\/"); slash != std::string::npos) nm = nm.substr (slash + 1);
                if (const auto dot = nm.find_last_of ('.'); dot != std::string::npos) nm = nm.substr (0, dot);
                std::memset (shared->clientName, 0, sizeof (shared->clientName));
                std::strncpy (shared->clientName, nm.c_str(), sizeof (shared->clientName) - 1);
                shared->driverClientId.fetch_add (1);
                shared->driverRunning = 1;
            }
            worker = std::thread ([this] { run(); });
            return ASE_OK;
        }
        ASIOError stop() override
        {
            stopThread();
            return ASE_OK;
        }

        // 24 each way, always: the same slot in shared memory as the channel number
        long numIns() const  { return route::channels; }
        long numOuts() const { return route::channels; }
        int inSlot (long c) const  { return c >= 0 && c < route::channels ? (int) c : -1; }
        int outSlot (long c) const { return c >= 0 && c < route::channels ? (int) c : -1; }

        ASIOError getChannels (long* numIn, long* numOut) override
        {
            if (numIn)  *numIn  = numIns();
            if (numOut) *numOut = numOuts();
            return ASE_OK;
        }
        ASIOError getLatencies (long* inLat, long* outLat) override
        {
            // our block, plus a block of MELVILLE's side of the ring
            if (inLat)  *inLat  = block;
            if (outLat) *outLat = block * 2;
            return ASE_OK;
        }
        ASIOError getBufferSize (long* minSize, long* maxSize, long* preferred, long* granularity) override
        {
            if (minSize) *minSize = 32;
            if (maxSize) *maxSize = 4096;
            if (preferred) *preferred = 256;
            if (granularity) *granularity = -1;     // powers of two
            return ASE_OK;
        }

        int melvilleRate() const { return shared != nullptr ? (int) shared->sampleRate.load() : 0; }
        bool melvilleAlive() const
        {
            return shared != nullptr && shared->sampleRate.load() > 0 && nowMs() - shared->heartbeatMs.load() < 250;
        }

        ASIOError canSampleRate (ASIOSampleRate sr) override
        {
            const int hr = melvilleRate();
            if (hr > 0) return std::fabs (sr - hr) < 1.0 ? ASE_OK : ASE_NoClock;
            for (double r : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
                if (std::fabs (sr - r) < 1.0) return ASE_OK;
            return ASE_NoClock;
        }
        ASIOError getSampleRate (ASIOSampleRate* sr) override
        {
            if (sr == nullptr) return ASE_InvalidParameter;
            const int hr = melvilleRate();
            *sr = hr > 0 ? (double) hr : rate;
            return ASE_OK;
        }
        ASIOError setSampleRate (ASIOSampleRate sr) override
        {
            if (canSampleRate (sr) != ASE_OK) return ASE_NoClock;   // follows MELVILLE's rate
            rate = sr;
            return ASE_OK;
        }
        ASIOError getClockSources (ASIOClockSource* clocks, long* num) override
        {
            if (clocks == nullptr || num == nullptr || *num < 1) return ASE_InvalidParameter;
            clocks[0].index = 0;
            clocks[0].associatedChannel = -1;
            clocks[0].associatedGroup = -1;
            clocks[0].isCurrentSource = ASIOTrue;
            std::strcpy (clocks[0].name, "MELVILLE");
            *num = 1;
            return ASE_OK;
        }
        ASIOError setClockSource (long ref) override { return ref == 0 ? ASE_OK : ASE_InvalidParameter; }

        ASIOError getSamplePosition (ASIOSamples* pos, ASIOTimeStamp* ts) override
        {
            const unsigned long long p = (unsigned long long) switchCount.load() * (unsigned long long) block;
            const unsigned long long ns = (unsigned long long) lastSwitchNs.load();
            if (pos) { pos->hi = (unsigned long) (p >> 32); pos->lo = (unsigned long) (p & 0xffffffffULL); }
            if (ts)  { ts->hi = (unsigned long) (ns >> 32); ts->lo = (unsigned long) (ns & 0xffffffffULL); }
            return ASE_OK;
        }

        ASIOError getChannelInfo (ASIOChannelInfo* info) override
        {
            if (info == nullptr) return ASE_InvalidParameter;
            const bool in = info->isInput != ASIOFalse;
            const long n = in ? numIns() : numOuts();
            if (info->channel < 0 || info->channel >= n) return ASE_InvalidParameter;
            info->isActive = ASIOFalse;
            for (auto& b : bufs) if ((b.isInput != 0) == in && b.channel == info->channel) info->isActive = ASIOTrue;
            info->channelGroup = 0;
            info->type = ASIOSTFloat32LSB;
            std::snprintf (info->name, sizeof (info->name), in ? "From MELVILLE %ld" : "To MELVILLE %ld", info->channel + 1);
            return ASE_OK;
        }

        ASIOError createBuffers (ASIOBufferInfo* infos, long numChannels, long bufferSize, ASIOCallbacks* cb) override
        {
            if (running) return ASE_InvalidMode;
            disposeBuffers();
            if (infos == nullptr || cb == nullptr || bufferSize < 16 || bufferSize > 8192) return ASE_InvalidParameter;
            block = bufferSize;
            callbacks = cb;
            for (long i = 0; i < numChannels; ++i)
            {
                auto& in = infos[i];
                const bool isIn = in.isInput != ASIOFalse;
                const long n = isIn ? numIns() : numOuts();
                if (in.channelNum < 0 || in.channelNum >= n) { disposeBuffers(); return ASE_InvalidParameter; }
                Buf b;
                b.isInput = isIn ? 1 : 0;
                b.channel = in.channelNum;
                b.slot = isIn ? inSlot (in.channelNum) : outSlot (in.channelNum);
                b.data.assign ((size_t) block * 2, 0.0f);
                bufs.push_back (std::move (b));
            }
            for (long i = 0; i < numChannels; ++i)
            {
                auto& b = bufs[(size_t) i];
                infos[i].buffers[0] = b.data.data();
                infos[i].buffers[1] = b.data.data() + block;
            }
            timeInfo = callbacks->asioMessage != nullptr
                       && callbacks->asioMessage (kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) != 0
                       && callbacks->asioMessage (kAsioSupportsTimeInfo, 0, nullptr, nullptr) != 0;
            buffersReady = true;
            return ASE_OK;
        }
        ASIOError disposeBuffers() override
        {
            stopThread();
            bufs.clear();
            buffersReady = false;
            callbacks = nullptr;
            return ASE_OK;
        }

        ASIOError controlPanel() override
        {
            char msg[512];
            const int hr = melvilleRate();
            std::snprintf (msg, sizeof (msg),
                           "MELVILLE ASIO\n\n"
                           "Your outputs 1-24 arrive on MELVILLE's patchbay (the MELVILLE ASIO panel's OUT jacks).\n"
                           "Your inputs 1-24 are whatever is patched to its IN jacks.\n\n"
                           "MELVILLE: %s\n"
                           "The sample rate follows MELVILLE's audio device; the buffer size is yours.",
                           melvilleAlive() ? (std::string ("running at ") + std::to_string (hr) + " Hz").c_str()
                                           : "not running (silent until it starts)");
            MessageBoxA (nullptr, msg, driverName, MB_OK | MB_ICONINFORMATION);
            return ASE_OK;
        }

        ASIOError future (long selector, void*) override
        {
            switch (selector)
            {
                case kAsioCanTimeInfo:  return ASE_SUCCESS;
                default:                return ASE_InvalidParameter;
            }
        }
        ASIOError outputReady() override { return ASE_NotPresent; }

    private:
        struct Buf { int isInput = 0; long channel = 0; int slot = 0; std::vector<float> data; };

        void openShared()
        {
            if (shared != nullptr) return;
            mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (route::Shared), route::mappingName);
            if (mapping == nullptr) return;
            shared = static_cast<route::Shared*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (route::Shared)));
            if (shared != nullptr && shared->magic != route::magic)
            {
                shared->magic = route::magic;
                shared->version = route::version;
            }
            tick = CreateEventW (nullptr, FALSE, FALSE, route::tickName);
        }
        void closeShared()
        {
            if (shared != nullptr) { shared->driverRunning = 0; UnmapViewOfFile (shared); shared = nullptr; }
            if (mapping != nullptr) { CloseHandle (mapping); mapping = nullptr; }
            if (tick != nullptr) { CloseHandle (tick); tick = nullptr; }
        }

        void stopThread()
        {
            running = false;
            if (tick != nullptr) SetEvent (tick);
            if (worker.joinable()) worker.join();
            if (shared != nullptr) shared->driverRunning = 0;
        }

        // One buffer: MELVILLE's sends in, the program's callback, its outputs to MELVILLE.
        void doSwitch (long long framePos, bool alive)
        {
            const int idx = (int) (switchCount.load() & 1);
            for (auto& b : bufs)
                if (b.isInput)
                {
                    float* dst = b.data.data() + (size_t) idx * (size_t) block;
                    if (alive && shared->fromWrite.load() >= framePos + block && framePos >= 0)
                        for (long k = 0; k < block; ++k)
                            dst[k] = shared->fromMelville[b.slot][(framePos + k) & route::ringMask];
                    else
                        std::memset (dst, 0, sizeof (float) * (size_t) block);
                }

            LARGE_INTEGER qpc, freq;
            QueryPerformanceCounter (&qpc);
            QueryPerformanceFrequency (&freq);
            lastSwitchNs = (long long) ((double) qpc.QuadPart * 1.0e9 / (double) freq.QuadPart);

            if (timeInfo && callbacks->bufferSwitchTimeInfo != nullptr)
            {
                ASIOTime t {};
                const unsigned long long p = (unsigned long long) switchCount.load() * (unsigned long long) block;
                t.timeInfo.samplePosition.hi = (unsigned long) (p >> 32);
                t.timeInfo.samplePosition.lo = (unsigned long) (p & 0xffffffffULL);
                const unsigned long long ns = (unsigned long long) lastSwitchNs.load();
                t.timeInfo.systemTime.hi = (unsigned long) (ns >> 32);
                t.timeInfo.systemTime.lo = (unsigned long) (ns & 0xffffffffULL);
                ASIOSampleRate sr = 0.0;
                getSampleRate (&sr);
                t.timeInfo.sampleRate = sr;
                t.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
                callbacks->bufferSwitchTimeInfo (&t, idx, ASIOTrue);
            }
            else if (callbacks->bufferSwitch != nullptr)
                callbacks->bufferSwitch (idx, ASIOTrue);

            // what the program played: on to MELVILLE
            if (shared != nullptr)
            {
                const long long w = shared->toWrite.load();
                for (int ch = 0; ch < route::channels; ++ch)
                {
                    const Buf* src = nullptr;
                    for (auto& b : bufs) if (! b.isInput && b.slot == ch) { src = &b; break; }
                    float* ring = shared->toMelville[ch];
                    for (long k = 0; k < block; ++k)
                        ring[(w + k) & route::ringMask] = src != nullptr ? src->data[(size_t) idx * (size_t) block + (size_t) k] : 0.0f;
                }
                shared->toWrite.store (w + block);
            }
            ++switchCount;
        }

        void run()
        {
            DWORD taskIndex = 0;
            HANDLE avrt = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);
            SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

            bool wasAlive = false;
            long long pos = 0;                  // MELVILLE frame our next buffer starts at
            LARGE_INTEGER freq, start, now;
            QueryPerformanceFrequency (&freq);
            QueryPerformanceCounter (&start);
            long long freeFrames = 0;

            while (running)
            {
                const bool alive = melvilleAlive();
                if (alive)
                {
                    const long long clk = shared->clock.load();
                    if (! wasAlive || clk - pos > 8LL * block || clk < pos - 8LL * block)
                        pos = clk - block;          // (re)lock onto MELVILLE's clock
                    wasAlive = true;
                    while (running && shared->clock.load() - pos >= block)
                    {
                        doSwitch (pos, true);
                        pos += block;
                    }
                    WaitForSingleObject (tick, 20);
                }
                else
                {
                    // MELVILLE isn't running: keep time ourselves, silently
                    if (wasAlive) { QueryPerformanceCounter (&start); freeFrames = 0; wasAlive = false; }
                    ASIOSampleRate sr = 48000.0;
                    getSampleRate (&sr);
                    QueryPerformanceCounter (&now);
                    const double elapsed = (double) (now.QuadPart - start.QuadPart) / (double) freq.QuadPart;
                    const long long due = (long long) (elapsed * sr);
                    if (due - freeFrames >= block)
                    {
                        doSwitch (-1, false);
                        freeFrames += block;
                    }
                    else
                        Sleep (1);
                }
            }
            if (avrt != nullptr) AvRevertMmThreadCharacteristics (avrt);
        }

        std::atomic<long> refs { 1 };
        std::atomic<bool> running { false };
        std::atomic<long long> switchCount { 0 }, lastSwitchNs { 0 };
        std::thread worker;
        std::vector<Buf> bufs;
        ASIOCallbacks* callbacks = nullptr;
        bool buffersReady = false, timeInfo = false;
        long block = 256;
        double rate = 48000.0;
        char error[128] {};

        HANDLE mapping = nullptr, tick = nullptr;
        route::Shared* shared = nullptr;
    };

    //==========================================================================
    class Factory : public IClassFactory
    {
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppv) override
        {
            if (ppv == nullptr) return E_POINTER;
            if (IsEqualIID (riid, IID_IUnknown) || IsEqualIID (riid, IID_IClassFactory)) { *ppv = this; return S_OK; }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override  { return 2; }
        ULONG STDMETHODCALLTYPE Release() override { return 1; }
        HRESULT STDMETHODCALLTYPE CreateInstance (IUnknown* outer, REFIID riid, void** ppv) override
        {
            if (outer != nullptr) return CLASS_E_NOAGGREGATION;
            auto* d = new MelvilleAsioDriver();
            const HRESULT hr = d->QueryInterface (riid, ppv);
            d->Release();
            return hr;
        }
        HRESULT STDMETHODCALLTYPE LockServer (BOOL lock) override
        {
            if (lock) ++lockCount; else --lockCount;
            return S_OK;
        }
    };
    Factory factory;

    bool setKey (HKEY root, const char* path, const char* name, const char* value)
    {
        HKEY k = nullptr;
        if (RegCreateKeyExA (root, path, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;
        const LONG r = RegSetValueExA (k, name, 0, REG_SZ, (const BYTE*) value, (DWORD) std::strlen (value) + 1);
        RegCloseKey (k);
        return r == ERROR_SUCCESS;
    }
}

//==============================================================================
extern "C" BOOL WINAPI DllMain (HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { thisModule = instance; DisableThreadLibraryCalls (instance); }
    return TRUE;
}

STDAPI DllGetClassObject (REFCLSID clsid, REFIID riid, void** ppv)
{
    if (! IsEqualCLSID (clsid, CLSID_MelvilleAsio)) return CLASS_E_CLASSNOTAVAILABLE;
    return factory.QueryInterface (riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return objectCount.load() == 0 && lockCount.load() == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    char path[MAX_PATH];
    GetModuleFileNameA (thisModule, path, MAX_PATH);
    const std::string clsidKey = std::string ("SOFTWARE\\Classes\\CLSID\\") + clsidString;
    const std::string asioKey  = std::string ("SOFTWARE\\ASIO\\") + driverName;
    const bool ok = setKey (HKEY_LOCAL_MACHINE, clsidKey.c_str(), nullptr, driverName)
                 && setKey (HKEY_LOCAL_MACHINE, (clsidKey + "\\InprocServer32").c_str(), nullptr, path)
                 && setKey (HKEY_LOCAL_MACHINE, (clsidKey + "\\InprocServer32").c_str(), "ThreadingModel", "Apartment")
                 && setKey (HKEY_LOCAL_MACHINE, asioKey.c_str(), "CLSID", clsidString)
                 && setKey (HKEY_LOCAL_MACHINE, asioKey.c_str(), "Description", driverName);
    return ok ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer()
{
    const std::string clsidKey = std::string ("SOFTWARE\\Classes\\CLSID\\") + clsidString;
    const std::string asioKey  = std::string ("SOFTWARE\\ASIO\\") + driverName;
    RegDeleteKeyExA (HKEY_LOCAL_MACHINE, (clsidKey + "\\InprocServer32").c_str(), KEY_WOW64_64KEY, 0);
    RegDeleteKeyExA (HKEY_LOCAL_MACHINE, clsidKey.c_str(), KEY_WOW64_64KEY, 0);
    RegDeleteKeyExA (HKEY_LOCAL_MACHINE, asioKey.c_str(), KEY_WOW64_64KEY, 0);
    return S_OK;
}
