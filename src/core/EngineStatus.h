#pragma once
// Snapshot of what the engine is doing, published to the tray / CLI via IPC.

#include "audio/models/AudioFormat.h"
#include "core/FormatCompatibility.h"

#include <vector>

namespace audioslave
{
enum class EngineState
{
    running,
    paused,
    stopping,
    stopped
};

juce::String engineStateName (EngineState s); // "RUNNING", "PAUSED", ...

enum class ScanKind
{
    full,      // every endpoint, ignoring the failure backoff
    triggered  // device notification: skips endpoints that failed moments ago
};

// What happens (or would happen) to one device under the format policy.
enum class DeviceAction
{
    none,         // format standardization off / not applicable
    compliant,    // already at the chosen format
    apply,        // planned: will be changed to the chosen format
    applied,      // changed and verified
    ignore,       // incompatible, left alone (policy: ignore)
    disable,      // planned: incompatible, will be disabled
    disabled,     // incompatible, disabled and verified
    keepDisabled, // disabled earlier by Audioslave, still incompatible
    reenable,     // planned: disabled earlier by Audioslave, will be enabled again
    reenabled,    // enabled again and verified
    pending,      // would be disabled, but the policy is not confirmed yet
    leftEnabled,  // came back enabled too often: no longer disabled (no loop)
    unknown,      // the supported formats could not be determined: left alone
    failed        // a change was rejected or did not stick
};

juce::String deviceActionName (DeviceAction a); // "applied", "disabled", ...
DeviceAction deviceActionFromName (const juce::String& name);

struct DeviceReport
{
    juce::String id;
    juce::String name;
    EndpointFlow flow = EndpointFlow::unknown;
    EndpointState state = EndpointState::notPresent;
    bool isDefault = false;
    bool disabledByAudioslave = false;
    juce::String currentFormat;      // "48000 Hz / 16-bit" when known
    FormatCapabilities capabilities;
    Compatibility compatibility = Compatibility::unknown;
    DeviceAction action = DeviceAction::none;
    juce::String reason;             // English detail (logs, failures)
};

// Something the user may want to hear about (tray notification).
struct DeviceEvent
{
    juce::int64 sequence = 0;
    juce::int64 timeMs = 0;
    DeviceAction action = DeviceAction::none; // disabled | reenabled | leftEnabled | failed
    juce::String id;
    juce::String name;
    juce::String reason;
    bool interactive = false; // caused by a change the user just applied
};

struct ScanReport
{
    ScanKind kind = ScanKind::full;
    int endpointsScanned = 0;
    int exclusiveFixed = 0;
    int exclusiveAlreadyOff = 0;
    int exclusiveSkipped = 0;
    int exclusiveFailed = 0;   // unreadable / write or verify failure
    int formatApplied = 0;
    int formatCompliant = 0;
    int formatUnsupported = 0;
    int formatSkipped = 0;
    int formatFailed = 0;
    int devicesDisabled = 0;
    int devicesReenabled = 0;
    int namesRestored = 0;
    bool enumerationFailed = false;
    bool paused = false;       // aborted because monitoring was paused

    [[nodiscard]] int errors() const noexcept { return exclusiveFailed + formatFailed + (enumerationFailed ? 1 : 0); }
    [[nodiscard]] bool changedSomething() const noexcept
    {
        return exclusiveFixed > 0 || formatApplied > 0 || devicesDisabled > 0 || devicesReenabled > 0 || namesRestored > 0;
    }

    // Per-device format outcome of this pass (not published in the status).
    std::vector<DeviceReport> devices;
};

struct EndpointStatus
{
    juce::String id;
    juce::String name;
    EndpointFlow flow = EndpointFlow::unknown;
    EndpointState state = EndpointState::notPresent;
    bool isDefault = false;
    juce::String exclusive;   // "blocked" | "allowed" | "unknown"
    juce::String format;      // "48000 Hz / 24-bit" (active endpoints, when known)
    juce::String description; // editable part of the name
    bool customName = false;  // a name chosen in Audioslave is kept on it
    bool disabledByAudioslave = false;
    // Format policy (active endpoints with format standardization on, and
    // devices disabled by Audioslave).
    Compatibility compatibility = Compatibility::unknown;
    juce::String capabilities; // FormatCapabilities::serialise()
    DeviceAction action = DeviceAction::none;
    juce::String reason;
};

struct EngineStatus
{
    EngineState state = EngineState::stopped;
    bool hasScanned = false;
    juce::Time lastScanTime;
    ScanReport lastScan;
    juce::int64 totalScans = 0;
    juce::int64 totalExclusiveFixes = 0;
    juce::int64 totalFormatChanges = 0;
    std::vector<EndpointStatus> endpoints;
    // Incompatible devices that would be disabled once the policy is confirmed.
    std::vector<DeviceReport> pendingDisable;
    std::vector<DeviceEvent> events; // most recent last
};
} // namespace audioslave
