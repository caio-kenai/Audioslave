#pragma once
// Installer / uninstaller system steps (Windows native: SCM, registry,
// shortcuts, ACLs, self-deleting uninstaller). The UI lives in SetupWizard.
//
// Installed layout:
//   <dir>\Audioslave.exe   tray (no arguments), service (--service), CLI
//   <dir>\Uninstall.exe    copy of the setup program
//   <dir>\logs\            audioslave.log, tray.log, setup.log
//   C:\ProgramData\Audioslave\config.ini
//   service "Audioslave" (automatic, LocalSystem, recovery)
//   HKLM Run "Audioslave" (tray for every user), Start Menu folder,
//   Apps & Features entry, event log source.

#include "installer/SetupOptions.h"

#include <functional>

namespace audioslave::setup
{
using Progress = std::function<void (int percent, const juce::String& text)>;

juce::File defaultInstallDir();

// Existing Audioslave installation folder (empty when not installed).
juce::String installedLocation();

// Runs on a worker thread. Returns false and sets `error` on failure.
bool install (const Options& options, const Progress& progress, juce::String& error);

// Removes everything; logs and configuration only with `removeData`.
bool uninstall (const juce::File& dir, bool removeData, juce::String& error);

// Uninstall.exe lives in the folder it removes: it copies itself to %TEMP%
// and continues from there. True when the copy was started.
// In silent mode it waits for the copy and returns its exit code in `exitCode`.
bool relaunchUninstallerFromTemp (const Options& options, const juce::File& dir, int* exitCode = nullptr);

// Stage 2 cannot delete itself: a detached cmd.exe removes it (and the
// folder, when empty) a few seconds later.
void deleteSelfLater (const juce::File& dir);

void appendSetupLog (const juce::File& dir, const juce::String& line);
} // namespace audioslave::setup
