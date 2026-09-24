#pragma once
// Service Control Manager operations (install / remove / start / stop /
// pause / continue / query). Windows native: JUCE has no service support.

#include <juce_core/juce_core.h>

#include <cstdint>

namespace audioslave::scm
{
enum class State
{
    notInstalled,
    stopped,
    startPending,
    stopPending,
    running,
    continuePending,
    pausePending,
    paused,
    unknown
};

juce::String stateName (State state);

struct Status
{
    State state = State::notInstalled;
    std::uint32_t processId = 0;
    bool acceptsStop = false;
    bool acceptsPauseContinue = false;
    bool acceptsPreshutdown = false;
    juce::String startType;   // "Automatic" | "Manual" | "Disabled"
    juce::String binaryPath;
    juce::String error;       // query failure (other than "not installed")
};

// `serviceName` defaults to Audioslave.
Status query (const wchar_t* serviceName = nullptr);
bool exists (const wchar_t* serviceName = nullptr);

// Installs the service, or refreshes the configuration of an existing one:
// binary path, automatic start, LocalSystem, description, recovery (restart
// after 5 s / 10 s / 30 s, reset after a day, also on non-crash failures)
// and a DACL that lets interactive users start / stop / pause / continue it.
// Non-fatal configuration problems are appended to `warnings`.
bool install (const juce::String& commandLine, juce::String& error, juce::StringArray& warnings);

// Stops (best effort) and deletes the service. True if it no longer exists.
bool uninstall (juce::String& error, const wchar_t* serviceName = nullptr);

bool start (juce::String& error);
// True when stopped (or already stopped). Waits up to `timeoutMs`.
bool stop (juce::String& error, int timeoutMs = 20000, const wchar_t* serviceName = nullptr);
bool restart (juce::String& error);
bool pause (juce::String& error);
bool resume (juce::String& error);
} // namespace audioslave::scm
