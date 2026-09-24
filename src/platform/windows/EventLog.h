#pragma once
// Windows Application event log (source "Audioslave").

#include <juce_core/juce_core.h>

namespace audioslave::win
{
enum class EventType
{
    information,
    warning,
    error
};

// Posts one message. Never throws; returns false when the log is unavailable.
bool writeEventLog (EventType type, const juce::String& message);
} // namespace audioslave::win
