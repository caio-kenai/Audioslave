#pragma once
// One Windows audio endpoint (render or capture) as seen by the watchdog.

#include "audio/models/ResultCode.h"

#include <juce_core/juce_core.h>

namespace audioslave
{
enum class EndpointFlow
{
    unknown,
    render,
    capture
};

// Mirrors DEVICE_STATE_* (mmdeviceapi.h) without including it.
enum class EndpointState
{
    active = 0x1,
    disabled = 0x2,
    notPresent = 0x4,
    unplugged = 0x8
};

juce::String endpointFlowName (EndpointFlow flow);
juce::String endpointStateName (EndpointState state);

struct ExclusiveModeState
{
    bool known = false;         // false when the property store could not be read
    bool allowed = false;       // "Allow applications to take exclusive control"
    bool priority = false;      // "Give exclusive mode applications priority"
    ResultCode readResult = result::ok;
};

struct AudioEndpoint
{
    juce::String id;            // stable endpoint id, e.g. {0.0.0.00000000}.{guid}
    juce::String name;          // friendly name
    juce::String description;   // editable part of the name (PKEY_Device_DeviceDesc)
    EndpointFlow flow = EndpointFlow::unknown;
    EndpointState state = EndpointState::notPresent;
    bool isDefault = false;     // default console endpoint for its flow
    ExclusiveModeState exclusive;

    [[nodiscard]] bool isActive() const noexcept { return state == EndpointState::active; }
};
} // namespace audioslave
