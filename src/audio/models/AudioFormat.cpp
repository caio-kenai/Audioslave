#include "audio/models/AudioFormat.h"

namespace audioslave
{
juce::String describeFormat (const AudioFormat& format)
{
    return juce::String (format.sampleRate) + " Hz / " + juce::String (format.validBits) + "-bit";
}

juce::String endpointFlowName (EndpointFlow flow)
{
    switch (flow)
    {
        case EndpointFlow::render:  return "render";
        case EndpointFlow::capture: return "capture";
        case EndpointFlow::unknown: break;
    }
    return "unknown";
}

juce::String endpointStateName (EndpointState state)
{
    switch (state)
    {
        case EndpointState::active:     return "active";
        case EndpointState::disabled:   return "disabled";
        case EndpointState::notPresent: return "not present";
        case EndpointState::unplugged:  return "unplugged";
    }
    return "unknown";
}
} // namespace audioslave
