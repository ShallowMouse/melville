#include "Taskbar.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shobjidl.h>
 #include <shellapi.h>
 #include <propkey.h>
 #include <propvarutil.h>
 #pragma comment (lib, "shell32.lib")
 #pragma comment (lib, "propsys.lib")
 #pragma comment (lib, "ole32.lib")
#endif

namespace melville::taskbar
{

static constexpr const char* processId = "PEQUOD.MELVILLE";

#if JUCE_WINDOWS
namespace
{
    // An .ico holding one 256px PNG (Windows Vista and later read these), for the pin's icon
    juce::File writeIcon (const juce::String& partId, const juce::Image& icon)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MELVILLE").getChildFile ("Icons");
        dir.createDirectory();
        auto file = dir.getChildFile (partId + ".ico");

        juce::MemoryOutputStream png;
        juce::PNGImageFormat().writeImageToStream (icon.rescaled (256, 256, juce::Graphics::highResamplingQuality), png);

        juce::MemoryOutputStream ico;
        ico.writeShort (0); ico.writeShort (1); ico.writeShort (1);          // header: icon, 1 image
        ico.writeByte (0); ico.writeByte (0);                                 // 256 x 256
        ico.writeByte (0); ico.writeByte (0);                                 // no palette
        ico.writeShort (1); ico.writeShort (32);                              // planes, bits
        ico.writeInt ((int) png.getDataSize());
        ico.writeInt (6 + 16);                                                // data offset
        ico.write (png.getData(), png.getDataSize());

        juce::MemoryBlock existing;
        if (! file.loadFileAsData (existing) || existing != ico.getMemoryBlock())
            file.replaceWithData (ico.getData(), ico.getDataSize());
        return file;
    }

    void setString (IPropertyStore* store, const PROPERTYKEY& key, const juce::String& value)
    {
        PROPVARIANT pv;
        if (SUCCEEDED (InitPropVariantFromString (value.toWideCharPointer(), &pv)))
        {
            store->SetValue (key, pv);
            PropVariantClear (&pv);
        }
    }
}
#endif

void setProcessIdentity()
{
   #if JUCE_WINDOWS
    SetCurrentProcessExplicitAppUserModelID (juce::String (processId).toWideCharPointer());
   #endif
}

void setWindowIdentity (juce::DocumentWindow& window, const juce::String& partId, const juce::String& displayName, const juce::Image& icon)
{
    if (icon.isValid())
        window.setIcon (icon);    // the title bar, Alt-Tab and the taskbar button

   #if JUCE_WINDOWS
    auto* hwnd = (HWND) window.getWindowHandle();
    if (hwnd == nullptr) return;

    IPropertyStore* store = nullptr;
    if (FAILED (SHGetPropertyStoreForWindow (hwnd, IID_PPV_ARGS (&store))) || store == nullptr) return;

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    const auto suffix = partId.substring (0, 1).toUpperCase() + partId.substring (1);
    setString (store, PKEY_AppUserModel_ID, juce::String (processId) + "." + suffix);
    // pinning: the pin opens MELVILLE (or brings it up) showing this part
    setString (store, PKEY_AppUserModel_RelaunchCommand, "\"" + exe + "\" --show=" + partId);
    setString (store, PKEY_AppUserModel_RelaunchDisplayNameResource, displayName);
    if (icon.isValid())
        setString (store, PKEY_AppUserModel_RelaunchIconResource, writeIcon (partId, icon).getFullPathName() + ",0");
    store->Commit();
    store->Release();
   #else
    juce::ignoreUnused (partId, displayName);
   #endif
}

} // namespace melville::taskbar
