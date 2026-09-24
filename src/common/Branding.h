#pragma once
// Product identity: names shared by the service, the tray, the CLI and the
// installer. Keep every user-visible or system-registered name here.

namespace audioslave::brand
{
inline constexpr const char* productName = "Audioslave";
inline constexpr const char* publisher = "caio-kenai";
inline constexpr const char* homepage = "https://github.com/caio-kenai/Audioslave";

// Windows service (SCM).
inline constexpr const wchar_t* serviceName = L"Audioslave";
inline constexpr const wchar_t* serviceDisplayName = L"Audioslave";
inline constexpr const wchar_t* serviceDescription =
    L"Monitors Windows audio devices and prevents applications from using exclusive audio mode.";

// Application event log source.
inline constexpr const wchar_t* eventSource = L"Audioslave";

// Executables (installed side by side).
inline constexpr const char* trayExecutable = "Audioslave.exe";
inline constexpr const char* serviceExecutable = "AudioslaveService.exe";
inline constexpr const char* uninstallerExecutable = "Uninstall.exe";

// Control channel between the tray / CLI and the service (\\.\pipe\<name>).
inline constexpr const char* controlPipeName = "Audioslave.Control";

// Per-session tray objects (Local\ namespace).
inline constexpr const wchar_t* trayMutexName = L"Local\\Audioslave.Tray";
inline constexpr const wchar_t* trayShowEventName = L"Local\\Audioslave.Tray.Show";
inline constexpr const wchar_t* trayQuitEventName = L"Local\\Audioslave.Tray.Quit";

// Required tooltip text (UTF-8) - do not change.
inline constexpr const char* trayTooltipUtf8 = "O Audioslave está em execução";

// Predecessor product, migrated by the installer.
inline constexpr const wchar_t* legacyServiceName = L"AudioWatchdog";
inline constexpr const wchar_t* legacyProductName = L"Audio Watchdog";
} // namespace audioslave::brand
