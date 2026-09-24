#pragma once
// Behavioural validation of the exclusive-mode policy with JUCE.
//
// Opens an endpoint through JUCE's "Windows Audio (Exclusive Mode)" device
// type - the very path a JUCE player takes to grab a device exclusively. If
// the open fails, no JUCE application can take the device exclusively.
//
// When exclusive mode is still allowed the probe really does open the device
// exclusively for a moment, so it only runs on demand (`diagnose
// --probe-exclusive`, `validate`), never periodically inside the service.
//
// Needs a JUCE message manager on the calling thread
// (juce::ScopedJuceInitialiser_GUI) and COM initialised.

#include "audio/models/AudioEndpoint.h"

namespace audioslave
{
struct ExclusiveProbeResult
{
    enum class Outcome
    {
        blocked,    // JUCE could not open the device exclusively (policy holds)
        allowed,    // JUCE opened the device exclusively (policy NOT effective)
        notFound,   // no JUCE device with that name (e.g. unplugged)
        unavailable // WASAPI exclusive type not available
    };

    Outcome outcome = Outcome::unavailable;
    juce::String juceDeviceName;
    juce::String detail;
};

juce::String probeOutcomeName (ExclusiveProbeResult::Outcome outcome);

ExclusiveProbeResult probeExclusiveMode (const juce::String& endpointName, EndpointFlow flow);
} // namespace audioslave
