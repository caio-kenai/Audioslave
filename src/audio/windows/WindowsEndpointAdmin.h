#pragma once
// IEndpointAdmin for Windows endpoints:
//  - enable / disable: IPolicyConfig::SetEndpointVisibility, exactly what the
//    Sound panel's "Disable" / "Enable" does (the endpoint moves to
//    DEVICE_STATE_DISABLED; the hardware and its driver stay installed);
//  - name: PKEY_Device_DeviceDesc in the endpoint's property store, what the
//    Sound panel's rename writes; Windows derives the friendly name
//    ("<description> (<adapter>)") from it.
// Both need administrator rights (the service runs as LocalSystem).

#include "audio/AudioInterfaces.h"

namespace audioslave::win
{
class WindowsEndpointAdmin final : public IEndpointAdmin
{
public:
    ResultCode setEnabled (const juce::String& endpointId, bool enabled) override;
    ResultCode getState (const juce::String& endpointId, EndpointState& out) override;
    ResultCode getDescription (const juce::String& endpointId, juce::String& out) override;
    ResultCode setDescription (const juce::String& endpointId, const juce::String& description) override;
};
} // namespace audioslave::win
