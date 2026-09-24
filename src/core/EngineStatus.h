#pragma once
// Snapshot of what the engine is doing, published to the tray / CLI via IPC.

#include "audio/models/AudioFormat.h"

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
    bool enumerationFailed = false;
    bool paused = false;       // aborted because monitoring was paused

    [[nodiscard]] int errors() const noexcept { return exclusiveFailed + formatFailed + (enumerationFailed ? 1 : 0); }
    [[nodiscard]] bool changedSomething() const noexcept { return exclusiveFixed > 0 || formatApplied > 0; }
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
};
} // namespace audioslave
