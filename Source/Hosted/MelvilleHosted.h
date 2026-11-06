#pragma once

// What a part (Hawthorne, EMERSON, WHITMAN, THOREAU, PEQUOD Host) calls when it's built
// into MELVILLE. Each app has its own Source/Melville.h that includes this when
// MELVILLE_HOSTED is set, and does nothing when the app is built on its own:
//
//     if (! melville::attach (deviceManager, "emerson"))
//         ... the app's own device setup, as before ...
//
// attach() gives the app's AudioDeviceManager only the "MELVILLE" device type and opens
// the part's device on it: the part then runs inside MELVILLE's hub, at the interface's
// rate and block, with the patchbay on its inputs and outputs.

#include <juce_audio_devices/juce_audio_devices.h>

namespace melville
{
    /** Is this program MELVILLE (rather than the app on its own)? */
    bool isHosted() noexcept;

    /** Open the part's MELVILLE device on this manager. False if there's no hub (then
        set the device up as the app would on its own). Message thread. */
    bool attach (juce::AudioDeviceManager&, const char* partId);

    /** The part's manager is going away (call before it's destroyed). */
    void detach (juce::AudioDeviceManager&);
}
