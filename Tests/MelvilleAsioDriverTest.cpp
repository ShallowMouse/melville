// Tests the MELVILLE ASIO driver without the registry: loads the DLL, plays "MELVILLE"
// (the shared memory + its clock) and "REAPER" (the ASIO host) at once, and checks every
// sample arrives in order, both ways. Windows only (also runs under Wine).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cmath>
#include <atomic>
#include <thread>
#include <vector>
#include <string>

#include "asiosys.h"
#include "asio.h"
#include "iasiodrv.h"
#include "../Source/Asio/MelvilleAsioShared.h"

namespace route = melville::asio;

static const CLSID clsid = { 0x181ac03e, 0x0abe, 0x4b6c, { 0xb9, 0xaf, 0xc4, 0x4e, 0xd4, 0x4c, 0xb4, 0xc3 } };
static int failures = 0;
#define CHECK(c, ...) do { if (! (c)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

// --- the "host" (Reaper) side
static IASIO* drv = nullptr;
static std::vector<ASIOBufferInfo> infos;
static long hostBlock = 128;
static std::atomic<long long> hostCount { 0 };        // samples the host has produced
static std::atomic<long long> hostSwitches { 0 };
static std::vector<float> hostIn;                      // what the host received from MELVILLE (ch 0)

static float hostValue (long long n) { return (float) ((n % 100000) + 1) / 100000.0f; }       // host output, ch 0
static float hawValue (long long n)  { return -(float) ((n % 100000) + 1) / 100000.0f; }      // MELVILLE master L

static void bufferSwitch (long index, ASIOBool)
{
    for (auto& bi : infos)
    {
        float* b = static_cast<float*> (bi.buffers[index]);
        if (! bi.isInput && bi.channelNum == 0)
            for (long k = 0; k < hostBlock; ++k) b[k] = hostValue (hostCount + k);
        if (bi.isInput && bi.channelNum == 23)               // MELVILLE's 24th send
            for (long k = 0; k < hostBlock; ++k) hostIn.push_back (b[k]);
    }
    hostCount += hostBlock;
    ++hostSwitches;
}
static std::atomic<int> resetRequests { 0 };
static long asioMessage (long sel, long val, void*, double*)
{
    if (sel == kAsioSelectorSupported) return val == kAsioResetRequest ? 1 : 0;
    if (sel == kAsioResetRequest) { ++resetRequests; return 1; }
    return 0;
}
static ASIOTime* switchTI (ASIOTime*, long i, ASIOBool d) { bufferSwitch (i, d); return nullptr; }

int main()
{
    std::printf ("MELVILLE ASIO driver test\n");

    // --- "MELVILLE": the shared memory, a 48 kHz clock in 64-sample blocks
    HANDLE map = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (route::Shared), route::mappingName);
    auto* sh = static_cast<route::Shared*> (MapViewOfFile (map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (route::Shared)));
    HANDLE tick = CreateEventW (nullptr, FALSE, FALSE, route::tickName);
    sh->magic = route::magic; sh->version = route::version;
    sh->sampleRate = 48000;
    sh->heartbeatMs = (long long) GetTickCount64();

    // --- load the driver like a host would (without the registry)
    HMODULE dll = LoadLibraryA ("MelvilleAsio.dll");
    CHECK (dll != nullptr, "load the DLL");
    if (dll == nullptr) return 1;
    using GetCO = HRESULT (__stdcall*) (REFCLSID, REFIID, void**);
    auto getCO = (GetCO) GetProcAddress (dll, "DllGetClassObject");
    CHECK (getCO != nullptr, "DllGetClassObject exported");
    IClassFactory* f = nullptr;
    CHECK (getCO (clsid, IID_IClassFactory, (void**) &f) == S_OK, "class factory");
    CHECK (f->CreateInstance (nullptr, clsid, (void**) &drv) == S_OK && drv != nullptr, "create the driver (CLSID as the interface, as ASIO hosts do)");

    CHECK (drv->init (nullptr) == ASIOTrue, "init");
    char name[64] {};
    drv->getDriverName (name);
    CHECK (std::string (name) == "MELVILLE ASIO", "name %s", name);
    long ni = 0, no = 0;
    drv->getChannels (&ni, &no);
    CHECK (ni == 24 && no == 24, "24 channels each way: %ld in / %ld out", ni, no);
    CHECK (drv->canSampleRate (48000.0) == ASE_OK && drv->canSampleRate (44100.0) != ASE_OK, "follows MELVILLE's rate");

    hostIn.reserve (48000 * 10);
    infos.resize (4);
    infos[0] = { ASIOFalse, 0, { nullptr, nullptr } };
    infos[1] = { ASIOFalse, 1, { nullptr, nullptr } };
    infos[2] = { ASIOTrue, 23, { nullptr, nullptr } };
    infos[3] = { ASIOTrue, 0, { nullptr, nullptr } };
    ASIOCallbacks cb { bufferSwitch, nullptr, asioMessage, switchTI };
    CHECK (drv->createBuffers (infos.data(), 4, hostBlock, &cb) == ASE_OK, "createBuffers");
    ASIOChannelInfo ci {};
    ci.channel = 0; ci.isInput = ASIOFalse;
    CHECK (drv->getChannelInfo (&ci) == ASE_OK && ci.type == ASIOSTFloat32LSB && ci.isActive, "channel info");
    ASIOChannelInfo a {};
    a.channel = 4; a.isInput = ASIOTrue;
    CHECK (drv->getChannelInfo (&a) == ASE_OK && std::string (a.name) == "From MELVILLE 5", "inputs named (%s)", a.name);
    ASIOChannelInfo b {};
    b.channel = 23; b.isInput = ASIOFalse;
    CHECK (drv->getChannelInfo (&b) == ASE_OK && std::string (b.name) == "To MELVILLE 24", "outputs named (%s)", b.name);
    ASIOChannelInfo c {};
    c.channel = 24; c.isInput = ASIOFalse;
    CHECK (drv->getChannelInfo (&c) != ASE_OK, "no 25th channel");
    CHECK (drv->start() == ASE_OK, "start");

    // MELVILLE's audio thread: write send 24, advance the clock, read what came in
    const int hb = 64;
    long long readPos = -1, got = 0, bad = 0, firstGood = -1;
    long long hawWritten = 0;
    for (int block = 0; block < 48000 * 3 / hb; ++block)   // 3 s of audio
    {
        for (int k = 0; k < hb; ++k) sh->fromMelville[23][(hawWritten + k) & route::ringMask] = hawValue (hawWritten + k);
        hawWritten += hb;
        sh->fromWrite.store (hawWritten);
        sh->clock.store (hawWritten);
        sh->heartbeatMs = (long long) GetTickCount64();
        SetEvent (tick);

        // reading side (as AsioNode does)
        const long long w = sh->toWrite.load();
        if (readPos < 0 && w > 0) readPos = w - hostBlock - hb;
        if (readPos >= 0)
            for (int k = 0; k < hb; ++k, ++readPos)
            {
                if (readPos >= w) break;
                const float v = sh->toMelville[0][readPos & route::ringMask];
                const float expect = hostValue (readPos);
                ++got;
                if (std::fabs (v - expect) > 1.0e-6f) ++bad; else if (firstGood < 0) firstGood = readPos;
            }
        Sleep (block % 3 == 0 ? 2 : 1);      // ~ real time-ish, jittery
    }
    const size_t inBefore = hostIn.size();
    drv->stop();
    CHECK (sh->driverRunning.load() == 0, "stopped");

    CHECK (hostSwitches > 100, "host called back %lld times", (long long) hostSwitches.load());
    CHECK (got > 10000 && bad == 0, "host -> MELVILLE: %lld samples, %lld out of order", got, bad);
    CHECK (hostCount.load() <= hawWritten + hostBlock, "the host runs on MELVILLE's clock (%lld vs %lld)", hostCount.load(), hawWritten);

    // MELVILLE -> host: the master comes back in order (after the first block)
    long long inBad = 0, inN = 0;
    for (size_t i = (size_t) hostBlock * 2; i + 1 < inBefore; ++i, ++inN)
    {
        const float a = hostIn[i], b = hostIn[i + 1];
        if (a == 0.0f || b == 0.0f) { ++inBad; continue; }
        const float step = std::fabs (std::fabs (b) - std::fabs (a)) * 100000.0f;
        if (std::fabs (step - 1.0f) > 0.01f && std::fabs (b) > 2.0e-5f) ++inBad;
    }
    CHECK (inN > 10000 && inBad == 0, "MELVILLE -> host: %lld samples, %lld out of order", inN, inBad);

    // MELVILLE gone: the driver keeps time by itself
    sh->sampleRate = 0;
    const long long before = hostSwitches.load();
    CHECK (drv->start() == ASE_OK, "restart without MELVILLE");
    Sleep (500);
    drv->stop();
    CHECK (hostSwitches.load() - before > 50, "free-running without MELVILLE (%lld callbacks in 0.5 s)", hostSwitches.load() - before);

    drv->disposeBuffers();
    drv->Release();
    CHECK (std::string (sh->clientName).size() > 0, "the program's name is shared (%s)", sh->clientName);
    std::printf (failures == 0 ? "ALL MELVILLE ASIO TESTS PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
