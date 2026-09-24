#pragma once
// Per-endpoint memory of the engine:
//  - failure backoff: after a rejected change, notification-triggered scans
//    leave the endpoint alone for 30 s, so a driver that rejects a change
//    cannot cause a notification -> write -> notification loop (full scans
//    always retry);
//  - notices: a recurring warning (unsupported format, unreadable state) is
//    logged at WARN once, then at DEBUG until the message changes.
// Thread-safe. The clock is injectable for tests.

#include <juce_core/juce_core.h>

#include <functional>
#include <map>

namespace audioslave
{
class EndpointMemory
{
public:
    using Clock = std::function<juce::int64()>; // milliseconds, monotonic

    static constexpr juce::int64 failureBackoffMs = 30000;

    explicit EndpointMemory (Clock clock = {});

    [[nodiscard]] bool inBackoff (const juce::String& endpointId) const;
    void recordFailure (const juce::String& endpointId);
    void clearFailure (const juce::String& endpointId);

    // True the first time `message` is noticed under `key` (or when it changed).
    bool notice (const juce::String& key, const juce::String& message);
    void clearNotice (const juce::String& key);

    // An endpoint was added / removed / re-plugged: forget everything about it.
    void forgetEndpoint (const juce::String& endpointId);

    // Configuration reload / resume.
    void clear();

private:
    Clock clock_;
    juce::CriticalSection lock_;
    std::map<juce::String, juce::int64> failures_;
    std::map<juce::String, juce::String> notices_;
};
} // namespace audioslave
