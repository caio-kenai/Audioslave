#pragma once
// Seams between the watchdog logic (core/) and the Windows audio stack
// (audio/windows/). The core only sees these interfaces, so every policy is
// unit-tested with in-memory fakes and no device is ever touched by a test.

#include "audio/models/AudioFormat.h"
#include "audio/models/ResultCode.h"

#include <vector>

namespace audioslave
{
class IAudioEndpointEnumerator
{
public:
    virtual ~IAudioEndpointEnumerator() = default;

    // Active and unplugged render + capture endpoints. result::notFound when
    // there are none (not an error: e.g. a server without audio hardware).
    virtual ResultCode enumerate (std::vector<AudioEndpoint>& out) = 0;
};

// Per-endpoint exclusive-mode policy (the two Sound-panel checkboxes).
class IExclusiveModeStore
{
public:
    virtual ~IExclusiveModeStore() = default;

    virtual ResultCode read (const juce::String& endpointId, bool& allow, bool& priority) = 0;
    virtual ResultCode write (const juce::String& endpointId, bool allow, bool priority) = 0;
};

// Per-endpoint shared-mode default format.
class IAudioFormatStore
{
public:
    virtual ~IAudioFormatStore() = default;

    virtual ResultCode getDeviceFormat (const juce::String& endpointId, AudioFormat& out) = 0;

    // Answered by the driver; independent of the exclusive-mode policy.
    virtual ResultCode isFormatSupported (const juce::String& endpointId, const AudioFormat& format, bool& supported) = 0;

    // Asks about many formats at once (`supported` gets one entry per format).
    // Implementations may override it to reach the driver only once.
    virtual ResultCode probeFormats (const juce::String& endpointId, const std::vector<AudioFormat>& formats,
                                     std::vector<bool>& supported)
    {
        supported.assign (formats.size(), false);
        for (size_t i = 0; i < formats.size(); ++i)
        {
            bool ok = false;
            if (const auto rc = isFormatSupported (endpointId, formats[i], ok); failed (rc))
                return rc;
            supported[i] = ok;
        }
        return result::ok;
    }

    // Applied by the audio engine immediately (like the Sound control panel).
    virtual ResultCode setDeviceFormat (const juce::String& endpointId, const AudioFormat& format) = 0;
};
} // namespace audioslave
