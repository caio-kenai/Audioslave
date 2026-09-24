#include "audio/juce/JuceExclusiveModeProbe.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>

namespace audioslave
{
juce::String probeOutcomeName (ExclusiveProbeResult::Outcome outcome)
{
    switch (outcome)
    {
        case ExclusiveProbeResult::Outcome::blocked:     return "BLOCKED";
        case ExclusiveProbeResult::Outcome::allowed:     return "ALLOWED";
        case ExclusiveProbeResult::Outcome::notFound:    return "NOT FOUND";
        case ExclusiveProbeResult::Outcome::unavailable: return "UNAVAILABLE";
    }
    return "UNAVAILABLE";
}

ExclusiveProbeResult probeExclusiveMode (const juce::String& endpointName, EndpointFlow flow)
{
    ExclusiveProbeResult result;
    std::unique_ptr<juce::AudioIODeviceType> type (
        juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::exclusive));
    if (type == nullptr)
    {
        result.detail = "JUCE has no WASAPI exclusive device type on this system";
        return result;
    }

    type->scanForDevices();
    const bool isInput = flow == EndpointFlow::capture;
    const auto names = type->getDeviceNames (isInput);

    // JUCE appends " (2)", " (3)" ... to duplicate names; prefer the exact one.
    auto name = names.contains (endpointName) ? endpointName : juce::String();
    if (name.isEmpty())
    {
        for (const auto& candidate : names)
            if (candidate.startsWith (endpointName + " ("))
            {
                name = candidate;
                break;
            }
    }
    if (name.isEmpty())
    {
        result.outcome = ExclusiveProbeResult::Outcome::notFound;
        result.detail = "JUCE lists no " + juce::String (isInput ? "input" : "output") + " device named '" + endpointName + "'";
        return result;
    }
    result.juceDeviceName = name;

    std::unique_ptr<juce::AudioIODevice> device (type->createDevice (isInput ? juce::String() : name,
                                                                     isInput ? name : juce::String()));
    if (device == nullptr)
    {
        result.outcome = ExclusiveProbeResult::Outcome::blocked;
        result.detail = "JUCE could not initialise the device for exclusive use";
        return result;
    }

    juce::BigInteger channels;
    channels.setRange (0, 2, true);
    const auto rates = device->getAvailableSampleRates();
    const double rate = rates.isEmpty() ? 48000.0 : rates.getFirst();

    const auto error = device->open (isInput ? channels : juce::BigInteger(), isInput ? juce::BigInteger() : channels,
                                     rate, device->getDefaultBufferSize());
    if (error.isEmpty())
    {
        device->close();
        result.outcome = ExclusiveProbeResult::Outcome::allowed;
        result.detail = "the device was opened in exclusive mode";
        return result;
    }

    result.outcome = ExclusiveProbeResult::Outcome::blocked;
    result.detail = "open() failed: " + error;
    return result;
}
} // namespace audioslave
