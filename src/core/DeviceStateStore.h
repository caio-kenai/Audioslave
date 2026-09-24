#pragma once
// The service's memory of the devices it disabled itself
// (C:\ProgramData\Audioslave\devices.json). Runtime state, not
// configuration: it is written only by the service.
//
// It is what makes the "disable incompatible devices" policy safe:
//   - only devices recorded here are ever re-enabled by Audioslave (a device
//     the user disabled is never touched);
//   - a disabled device cannot be asked for its formats, so the capabilities
//     seen before disabling it are kept to re-evaluate it when the chosen
//     format changes;
//   - the disable history bounds how often a device that keeps coming back
//     (re-enabled by the user, or re-created by Windows / its driver) is
//     disabled again, so the policy can never loop.
// Thread-safe.

#include "audio/models/AudioEndpoint.h"
#include "core/FormatCompatibility.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace audioslave
{
struct DisabledDeviceRecord
{
    juce::String id;
    juce::String name;
    EndpointFlow flow = EndpointFlow::unknown;
    juce::int64 disabledAtMs = 0;        // wall clock, ms since 1970
    juce::String requested;              // "48000 Hz / 24-bit"
    juce::String reason;                 // English, as logged
    Compatibility compatibility = Compatibility::unknown;
    FormatCapabilities capabilities;
    std::vector<juce::int64> history;    // times it was disabled (last 24 h)
    // It came back too often: left enabled until the configuration changes.
    bool leftEnabled = false;
    // Currently disabled by Audioslave (false once it came back enabled).
    bool disabled = true;

    bool operator== (const DisabledDeviceRecord&) const = default;
};

class DeviceStateStore
{
public:
    // `file` empty: memory only (tests, CLI).
    explicit DeviceStateStore (juce::File file = {});

    [[nodiscard]] std::optional<DisabledDeviceRecord> get (const juce::String& endpointId) const;
    [[nodiscard]] std::vector<DisabledDeviceRecord> all() const;
    void put (const DisabledDeviceRecord& record);
    void remove (const juce::String& endpointId);

    // The chosen format or the policy changed: devices left enabled get a new
    // chance and their history starts over.
    void resetHistory();

private:
    void load();
    void save() const;

    juce::File file_;
    mutable juce::CriticalSection lock_;
    std::vector<DisabledDeviceRecord> records_;
};
} // namespace audioslave
