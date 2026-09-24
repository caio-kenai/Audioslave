#pragma once
// IAudioFormatStore for Windows endpoints.
//
//  - current format: PKEY_AudioEngine_DeviceFormat from the property store;
//  - supported formats: WindowsFormatSupport (IKsFormatSupport, driver answer);
//  - applying a format: IPolicyConfig::SetDeviceFormat, the COM interface the
//    Sound control panel uses. Writing PKEY_AudioEngine_DeviceFormat through
//    IPropertyStore only changes the stored value; the audio engine keeps its
//    old mix format until it restarts (verified), so that path is not used.

#include "audio/AudioInterfaces.h"

namespace audioslave::win
{
class WindowsAudioFormatPolicy final : public IAudioFormatStore
{
public:
    ResultCode getDeviceFormat (const juce::String& endpointId, AudioFormat& out) override;
    ResultCode isFormatSupported (const juce::String& endpointId, const AudioFormat& format, bool& supported) override;
    ResultCode setDeviceFormat (const juce::String& endpointId, const AudioFormat& format) override;
};
} // namespace audioslave::win
