#pragma once
// A device notification forwarded by the device watcher to the engine.

#include "audio/models/AudioEndpoint.h"

namespace audioslave
{
struct DeviceChange
{
    enum class Kind
    {
        added,
        removed,
        stateChanged,
        defaultChanged,
        propertyChanged
    };

    Kind kind = Kind::propertyChanged;
    juce::String endpointId;
    EndpointState newState = EndpointState::active; // stateChanged only
};
} // namespace audioslave
