#pragma once
// Administrator-rights helpers.

#include <juce_core/juce_core.h>

namespace audioslave::win
{
// True when the process token is elevated.
bool isElevated();

// Starts `executable` with the "runas" verb (UAC prompt). Returns true when
// the elevated process was launched.
bool launchElevated (const juce::File& executable, const juce::StringArray& arguments);

// Quotes one command-line argument following the CommandLineToArgvW rules.
juce::String quoteArgument (const juce::String& argument);
} // namespace audioslave::win
