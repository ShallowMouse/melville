#pragma once

// Giving each part its own taskbar button, like a separate program. On Windows a window
// joins the taskbar group of its "AppUserModelID"; MELVILLE gives every part's window its
// own (PEQUOD.MELVILLE.Emerson ...), its own icon, and a relaunch command, so it gets its
// own button, can be pinned on its own, and opening the pin brings that part up in MELVILLE
// (MELVILLE.exe --show=emerson). Elsewhere this only sets the window icon.

#include <juce_gui_basics/juce_gui_basics.h>

namespace melville::taskbar
{
    /** Call once at startup, before any window: MELVILLE's own id for the bar. */
    void setProcessIdentity();

    /** Give a window its own taskbar button. Call before the window is first shown. */
    void setWindowIdentity (juce::DocumentWindow&, const juce::String& partId, const juce::String& displayName, const juce::Image& icon);
}
