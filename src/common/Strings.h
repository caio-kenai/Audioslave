#pragma once
// Small helpers around juce::String used across the project.

#include <juce_core/juce_core.h>

namespace audioslave
{
// juce::String's const char* constructor asserts on non-ASCII text; source
// files are UTF-8 (/utf-8), so user-visible literals go through here.
inline juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

inline juce::String boolText (bool value)
{
    return value ? "true" : "false";
}
} // namespace audioslave
