#pragma once
// User-facing (Portuguese) texts of the audio settings screen, built from the
// service's per-device reports. Pure functions, unit-tested.
//
//   before applying:  what the chosen format will do to every device
//                     (compatible / limited by bit depth / without the sample
//                     rate / ignored / disabled / enabled again);
//   after applying:   what could not be configured and why, and what was
//                     disabled.

#include "ipc/Protocol.h"

namespace audioslave
{
// "48000 Hz disponível, máximo de 16 bits" / "48000 Hz não disponível".
juce::String describeLimitation (const DeviceReport& device, std::uint32_t rate, std::uint16_t bits);

// "Reprodução" / "Captura".
juce::String flowLabel (EndpointFlow flow);

struct SettingsPreview
{
    bool needsConfirmation = false; // show the dialog (Continuar / Cancelar)
    bool disablesDevices = false;   // at least one device will be disabled
    juce::String title;
    juce::String message;           // short summary
    juce::String details;           // one section per group, device by device
};

SettingsPreview buildPreview (const ipc::AudioSettings& settings, const std::vector<DeviceReport>& devices);

struct SettingsOutcome
{
    bool problems = false;          // failures, ignored or disabled devices
    bool paused = false;
    juce::String title;
    juce::String message;
    juce::String details;
};

SettingsOutcome buildOutcome (const ipc::AudioSettings& settings, const std::vector<DeviceReport>& devices, bool paused);
} // namespace audioslave
