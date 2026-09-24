#pragma once
// IAudioEndpointEnumerator over the MMDevice API.
//
// Unlike juce::AudioIODeviceType (active endpoints only, identified by name),
// this returns stable endpoint ids and includes unplugged endpoints, so a
// jack that is re-plugged is already compliant.

#include "audio/AudioInterfaces.h"

namespace audioslave::win
{
class WindowsAudioEndpointEnumerator final : public IAudioEndpointEnumerator
{
public:
    ResultCode enumerate (std::vector<AudioEndpoint>& out) override;

    // Default console endpoint id for a flow (empty on failure).
    static juce::String defaultEndpointId (EndpointFlow flow);
};
} // namespace audioslave::win
