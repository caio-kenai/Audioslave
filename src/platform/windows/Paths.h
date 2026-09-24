#pragma once
// Well-known locations of an Audioslave installation.
//
//   <install dir>\Audioslave.exe (tray, service, CLI), Uninstall.exe
//   <install dir>\logs\audioslave.log | tray.log | setup.log
//   C:\ProgramData\Audioslave\config.ini

#include <juce_core/juce_core.h>

namespace audioslave::paths
{
juce::File executableFile();
juce::File installDir();

// Another executable installed next to the running one.
juce::File siblingExecutable (const juce::String& fileName);

juce::File programDataDir();
juce::File configFile();

// Audio Watchdog's configuration (C:\ProgramData\Audio Watchdog\config.ini),
// imported by the installer when Audioslave has none yet.
juce::File legacyConfigFile();

juce::File logsDir();
juce::File serviceLogFile();
juce::File trayLogFile();
juce::File setupLogFile (const juce::File& installFolder);

// Creates the folder (and parents). True when it exists afterwards.
bool ensureDirectory (const juce::File& dir);
} // namespace audioslave::paths

namespace audioslave::win
{
// Folder ACLs used by an installed copy (administrator rights required):
//   config folder: SYSTEM / Administrators full control, users read-only
//   logs folder:   SYSTEM / Administrators full control, users modify
bool applyConfigDirAcl (const juce::File& dir, juce::String* error = nullptr);
bool applyLogsDirAcl (const juce::File& dir, juce::String* error = nullptr);
} // namespace audioslave::win
