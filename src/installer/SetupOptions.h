#pragma once
// Command line of Audioslave-Setup.exe / Uninstall.exe.
//
//   (none)                  interactive wizard
//   /S                      silent install (keeps an existing configuration)
//   /format=48000:24        enable format standardization (rate:bits)
//   /noformat               disable format standardization
//   /dir="C:\path"          install folder
//   /notray                 do not start the tray application at the end
//   /uninstall [/S] [/removedata]
// Uninstall.exe runs the uninstaller without arguments.

#include <juce_core/juce_core.h>

#include <cstdint>

namespace audioslave::setup
{
struct Options
{
    bool silent = false;
    bool uninstall = false;
    bool uninstallStage2 = false;   // internal: running from %TEMP%
    bool removeData = false;
    bool launchTray = true;
    bool formatSet = false;          // /format or /noformat given
    bool format = false;
    std::uint32_t sampleRate = 48000;
    std::uint16_t bitDepth = 24;
    juce::String dir;
};

Options parseOptions (const juce::StringArray& arguments, const juce::String& executableName);
} // namespace audioslave::setup
