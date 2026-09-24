#pragma once
// Command-line interface of Audioslave.exe (juce::ConsoleApplication).

#include <juce_core/juce_core.h>

namespace audioslave::cli
{
// Runs the command in `args` (first argument = verb). Returns the exit code.
int run (const juce::ArgumentList& args);

// Writes UTF-8 text to stdout (the console code page is switched to UTF-8).
void print (const juce::String& text);
void printLine (const juce::String& text = {});
} // namespace audioslave::cli
