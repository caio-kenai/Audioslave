#include "platform/windows/WinCommon.h"
#include "installer/InstallerCore.h"
#include "AudioslaveVersion.h"
#include "common/Branding.h"
#include "common/Strings.h"
#include "config/Configuration.h"
#include "platform/windows/Elevation.h"
#include "platform/windows/Paths.h"
#include "platform/windows/ScopedComInit.h"
#include "platform/windows/SessionInstance.h"
#include "platform/windows/WinError.h"
#include "platform/windows/WinHandles.h"
#include "resource.h"
#include "service/ServiceController.h"

#include <exdisp.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace audioslave::setup
{
namespace
{
constexpr const wchar_t* runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* uninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Audioslave";
constexpr const wchar_t* eventLogKey = L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\Audioslave";
constexpr const wchar_t* runValue = L"Audioslave";

juce::File knownFolder (REFKNOWNFOLDERID id)
{
    win::CoTaskMemPtr<wchar_t> path;
    if (FAILED (::SHGetKnownFolderPath (id, 0, nullptr, path.put())) || ! path)
        return {};
    return juce::File (juce::String (path.get()));
}

juce::File startMenuDir()
{
    return knownFolder (FOLDERID_CommonPrograms).getChildFile (brand::productName);
}

juce::String regReadString (HKEY root, const wchar_t* key, const wchar_t* value)
{
    wchar_t buffer[2048] = {};
    DWORD bytes = sizeof (buffer);
    if (::RegGetValueW (root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &bytes) == ERROR_SUCCESS)
        return juce::String (buffer);
    return {};
}

bool regSetString (HKEY key, const wchar_t* name, const juce::String& value, DWORD type = REG_SZ)
{
    const auto* text = value.toWideCharPointer();
    return ::RegSetValueExW (key, name, 0, type, reinterpret_cast<const BYTE*> (text),
                             static_cast<DWORD> ((std::wcslen (text) + 1) * sizeof (wchar_t)))
           == ERROR_SUCCESS;
}

bool regSetDword (HKEY key, const wchar_t* name, DWORD value)
{
    return ::RegSetValueExW (key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*> (&value), sizeof (value)) == ERROR_SUCCESS;
}

void regDeleteValue (HKEY root, const wchar_t* key, const wchar_t* value)
{
    win::RegistryKey k;
    if (::RegOpenKeyExW (root, key, 0, KEY_SET_VALUE, k.put()) == ERROR_SUCCESS)
        ::RegDeleteValueW (k.get(), value);
}

// Deletes a file, or schedules it for deletion at reboot when it is in use.
void deleteOrSchedule (const juce::File& file)
{
    if (file.exists() && ! file.deleteFile())
        ::MoveFileExW (file.getFullPathName().toWideCharPointer(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
}

bool createShortcut (const juce::File& link, const juce::File& target, const juce::String& description)
{
    juce::ComSmartPtr<IShellLinkW> shellLink;
    if (FAILED (shellLink.CoCreateInstance (CLSID_ShellLink, CLSCTX_INPROC_SERVER)))
        return false;
    shellLink->SetPath (target.getFullPathName().toWideCharPointer());
    shellLink->SetWorkingDirectory (target.getParentDirectory().getFullPathName().toWideCharPointer());
    shellLink->SetDescription (description.toWideCharPointer());
    shellLink->SetIconLocation (target.getFullPathName().toWideCharPointer(), 0);
    juce::ComSmartPtr<IPersistFile> file;
    if (FAILED (shellLink.QueryInterface (file)))
        return false;
    return SUCCEEDED (file->Save (link.getFullPathName().toWideCharPointer(), TRUE));
}

// Starts a program as the logged-on user (not elevated) by asking the
// desktop shell (Explorer) to start it.
bool launchUnelevated (const juce::File& exe)
{
    const win::ScopedComInit com (COINIT_APARTMENTTHREADED);
    juce::ComSmartPtr<IShellWindows> windows;
    if (FAILED (windows.CoCreateInstance (CLSID_ShellWindows, CLSCTX_LOCAL_SERVER)))
        return false;

    VARIANT location {};
    VARIANT empty {};
    long hwnd = 0;
    juce::ComSmartPtr<IDispatch> dispatch;
    if (windows->FindWindowSW (&location, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, dispatch.resetAndGetPointerAddress()) != S_OK
        || dispatch == nullptr)
        return false;

    juce::ComSmartPtr<IServiceProvider> provider;
    juce::ComSmartPtr<IShellBrowser> browser;
    juce::ComSmartPtr<IShellView> view;
    juce::ComSmartPtr<IDispatch> background;
    juce::ComSmartPtr<IShellFolderViewDual> folderView;
    juce::ComSmartPtr<IDispatch> application;
    juce::ComSmartPtr<IShellDispatch2> shell;
    if (FAILED (dispatch.QueryInterface (provider))
        || FAILED (provider->QueryService (SID_STopLevelBrowser, IID_PPV_ARGS (browser.resetAndGetPointerAddress())))
        || FAILED (browser->QueryActiveShellView (view.resetAndGetPointerAddress()))
        || FAILED (view->GetItemObject (SVGIO_BACKGROUND, IID_PPV_ARGS (background.resetAndGetPointerAddress())))
        || FAILED (background.QueryInterface (folderView))
        || FAILED (folderView->get_Application (application.resetAndGetPointerAddress()))
        || FAILED (application.QueryInterface (shell)))
        return false;

    BSTR file = ::SysAllocString (exe.getFullPathName().toWideCharPointer());
    VARIANT none {};
    const bool ok = SUCCEEDED (shell->ShellExecute (file, none, none, none, none));
    ::SysFreeString (file);
    return ok;
}

bool writePayload (const juce::File& exe, juce::String& error)
{
    const HMODULE self = ::GetModuleHandleW (nullptr);
    const HRSRC resource = ::FindResourceW (self, MAKEINTRESOURCEW (IDR_PAYLOAD), RT_RCDATA);
    const HGLOBAL handle = resource != nullptr ? ::LoadResource (self, resource) : nullptr;
    const void* data = handle != nullptr ? ::LockResource (handle) : nullptr;
    const DWORD size = resource != nullptr ? ::SizeofResource (self, resource) : 0;
    if (data == nullptr || size == 0)
    {
        error = utf8 ("O instalador está corrompido (payload ausente).");
        return false;
    }

    // A running copy (a tray in another session) keeps the file locked;
    // renaming a running executable is allowed, so move it aside.
    if (exe.exists() && ! exe.deleteFile())
    {
        const auto old = exe.withFileExtension ("exe.old");
        old.deleteFile();
        if (::MoveFileExW (exe.getFullPathName().toWideCharPointer(), old.getFullPathName().toWideCharPointer(),
                           MOVEFILE_REPLACE_EXISTING))
            ::MoveFileExW (old.getFullPathName().toWideCharPointer(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    }

    const auto temp = exe.withFileExtension ("exe.new");
    if (! temp.replaceWithData (data, size) || ! temp.moveFileTo (exe))
    {
        error = utf8 ("Não foi possível gravar ") + exe.getFullPathName() + ": " + win::lastErrorText();
        temp.deleteFile();
        return false;
    }
    return true;
}

void registerEventSource (const juce::File& dir)
{
    // .NET's generic message file (present on every Windows 10/11) renders
    // the plain-text events written by the service.
    const juce::String dll ("%SystemRoot%\\Microsoft.NET\\Framework64\\v4.0.30319\\EventLogMessages.dll");
    wchar_t expanded[MAX_PATH] = {};
    ::ExpandEnvironmentStringsW (dll.toWideCharPointer(), expanded, MAX_PATH);
    if (! juce::File (juce::String (expanded)).existsAsFile())
    {
        appendSetupLog (dir, "Event log message file not found; events are written without a message file.");
        return;
    }
    win::RegistryKey key;
    if (::RegCreateKeyExW (HKEY_LOCAL_MACHINE, eventLogKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.put(), nullptr) != ERROR_SUCCESS
        || ! regSetString (key.get(), L"EventMessageFile", dll, REG_EXPAND_SZ) || ! regSetDword (key.get(), L"TypesSupported", 7))
        appendSetupLog (dir, "Could not register the event log source: " + win::lastErrorText());
}

void registerUninstallEntry (const juce::File& dir)
{
    win::RegistryKey key;
    if (::RegCreateKeyExW (HKEY_LOCAL_MACHINE, uninstallKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key.put(), nullptr) != ERROR_SUCCESS)
    {
        appendSetupLog (dir, "Could not create the Apps & Features entry: " + win::lastErrorText());
        return;
    }
    const auto exe = dir.getChildFile (brand::executableName);
    const auto uninstaller = win::quoteArgument (dir.getChildFile (brand::uninstallerExecutable).getFullPathName());
    regSetString (key.get(), L"DisplayName", brand::productName);
    regSetString (key.get(), L"DisplayVersion", AUDIOSLAVE_VERSION_STRING);
    regSetString (key.get(), L"Publisher", brand::publisher);
    regSetString (key.get(), L"DisplayIcon", exe.getFullPathName() + ",0");
    regSetString (key.get(), L"InstallLocation", dir.getFullPathName());
    regSetString (key.get(), L"UninstallString", uninstaller);
    regSetString (key.get(), L"QuietUninstallString", uninstaller + " /S");
    regSetString (key.get(), L"URLInfoAbout", brand::homepage);
    regSetDword (key.get(), L"NoModify", 1);
    regSetDword (key.get(), L"NoRepair", 1);
    regSetDword (key.get(), L"VersionMajor", AUDIOSLAVE_VERSION_MAJOR);
    regSetDword (key.get(), L"VersionMinor", AUDIOSLAVE_VERSION_MINOR);
    regSetDword (key.get(), L"EstimatedSize", static_cast<DWORD> (exe.getSize() / 1024 * 2));
}

} // namespace

void appendSetupLog (const juce::File& dir, const juce::String& line)
{
    const auto file = paths::setupLogFile (dir);
    file.getParentDirectory().createDirectory();
    file.appendText ("[" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "] " + line + "\r\n", false, false, "\r\n");
}

juce::File defaultInstallDir()
{
    const auto existing = installedLocation();
    if (existing.isNotEmpty())
        return juce::File (existing);
    return knownFolder (FOLDERID_ProgramFiles).getChildFile (brand::productName);
}

juce::String installedLocation()
{
    return regReadString (HKEY_LOCAL_MACHINE, uninstallKey, L"InstallLocation");
}

bool install (const Options& options, const Progress& progress, juce::String& error)
{
    const win::ScopedComInit com (COINIT_APARTMENTTHREADED);
    const juce::File dir (options.dir);
    const auto exe = dir.getChildFile (brand::executableName);
    const auto logs = dir.getChildFile ("logs");

    progress (5, "Preparando...");
    if (! paths::ensureDirectory (dir))
    {
        error = utf8 ("Não foi possível criar a pasta de instalação: ") + dir.getFullPathName();
        return false;
    }
    paths::ensureDirectory (logs);
    juce::String aclError;
    if (! win::applyLogsDirAcl (logs, &aclError))
        appendSetupLog (dir, "Logs folder permissions: " + aclError);
    appendSetupLog (dir, "Installing Audioslave " AUDIOSLAVE_VERSION_STRING " into " + dir.getFullPathName());

    // 1. Stop what is running (the service releases the executable).
    progress (18, utf8 ("Parando a versão em execução..."));
    win::SessionInstance::signalQuit(); // the tray of this session; others notice the update
    if (scm::exists())
    {
        juce::String stopError;
        if (! scm::stop (stopError))
            appendSetupLog (dir, "Could not stop the running service: " + stopError);
    }

    // 2. Binaries.
    progress (30, "Copiando arquivos...");
    if (! writePayload (exe, error))
        return false;
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto uninstaller = dir.getChildFile (brand::uninstallerExecutable);
    if (self != uninstaller && ! self.copyFileTo (uninstaller))
        appendSetupLog (dir, "Could not write Uninstall.exe: " + win::lastErrorText());

    // 3. Configuration: keep every existing setting, apply the choices.
    progress (45, utf8 ("Gravando configuração..."));
    paths::ensureDirectory (paths::programDataDir());
    if (! win::applyConfigDirAcl (paths::programDataDir(), &aclError))
        appendSetupLog (dir, "Config folder permissions: " + aclError);
    auto loaded = loadConfiguration (paths::configFile(), false);
    for (const auto& w : loaded.warnings)
        appendSetupLog (dir, w);
    auto cfg = loaded.config;
    cfg.exclusiveModeProtection = true; // mandatory feature
    if (options.formatSet)
    {
        cfg.formatStandardization = options.format;
        cfg.sampleRate = options.sampleRate;
        cfg.bitDepth = options.bitDepth;
    }
    if (auto saved = saveConfiguration (cfg, paths::configFile()); saved.failed())
    {
        error = utf8 ("Não foi possível gravar a configuração: ") + saved.getErrorMessage();
        return false;
    }
    appendSetupLog (dir, "Exclusive Mode Protection: ENABLED | Format Standardization: "
                             + (cfg.formatStandardization ? "ENABLED (" + describeFormatTarget (cfg) + ")" : juce::String ("DISABLED")));

    // 4. Windows service (automatic start, recovery, permissions).
    progress (60, utf8 ("Instalando o serviço do Windows..."));
    juce::StringArray warnings;
    juce::String serviceError;
    if (! scm::install (win::quoteArgument (exe.getFullPathName()) + " --service", serviceError, warnings))
    {
        error = utf8 ("Não foi possível instalar o serviço: ") + serviceError;
        appendSetupLog (dir, error);
        return false;
    }
    for (const auto& w : warnings)
        appendSetupLog (dir, "Service configuration warning: " + w);
    registerEventSource (dir);

    // 5. Tray for every user at logon, Start Menu, Apps & Features.
    progress (75, "Registrando atalhos...");
    {
        win::RegistryKey run;
        if (::RegCreateKeyExW (HKEY_LOCAL_MACHINE, runKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, run.put(), nullptr) != ERROR_SUCCESS
            || ! regSetString (run.get(), runValue, win::quoteArgument (exe.getFullPathName())))
            appendSetupLog (dir, "Could not register the tray at logon: " + win::lastErrorText());
    }
    const auto menu = startMenuDir();
    menu.createDirectory();
    if (! createShortcut (menu.getChildFile ("Audioslave.lnk"), exe, brand::productName)
        || ! createShortcut (menu.getChildFile ("Desinstalar Audioslave.lnk"), uninstaller, "Remove o Audioslave")
        || ! createShortcut (menu.getChildFile ("Logs do Audioslave.lnk"), logs, "Pasta de logs do Audioslave"))
        appendSetupLog (dir, "Some Start Menu shortcuts could not be created.");
    registerUninstallEntry (dir);

    // 6. Start.
    progress (90, utf8 ("Iniciando o serviço..."));
    if (! scm::start (serviceError))
    {
        error = utf8 ("O serviço foi instalado, mas não pôde ser iniciado: ") + serviceError;
        appendSetupLog (dir, error);
        return false;
    }
    if (options.launchTray && ! launchUnelevated (exe))
        appendSetupLog (dir, "The tray could not be started for the logged-on user.");

    appendSetupLog (dir, "Installation completed.");
    progress (100, utf8 ("Instalação concluída."));
    return true;
}

bool uninstall (const juce::File& dir, bool removeData, juce::String& error)
{
    appendSetupLog (dir, "Uninstalling Audioslave.");
    win::SessionInstance::signalQuit();

    if (! scm::uninstall (error))
    {
        error = utf8 ("Não foi possível remover o serviço: ") + error;
        appendSetupLog (dir, error);
        return false;
    }

    regDeleteValue (HKEY_LOCAL_MACHINE, runKey, runValue);
    regDeleteValue (HKEY_CURRENT_USER, runKey, runValue);
    ::RegDeleteKeyW (HKEY_LOCAL_MACHINE, uninstallKey);
    ::RegDeleteKeyW (HKEY_LOCAL_MACHINE, eventLogKey);
    startMenuDir().deleteRecursively();

    // Trays in other sessions notice the service is gone and exit; give them
    // a moment, then delete (or schedule) the binaries.
    juce::Thread::sleep (2500);
    deleteOrSchedule (dir.getChildFile (brand::executableName));
    deleteOrSchedule (dir.getChildFile ("Audioslave.exe.old"));
    deleteOrSchedule (dir.getChildFile (brand::uninstallerExecutable));

    if (removeData)
    {
        dir.getChildFile ("logs").deleteRecursively();
        paths::programDataDir().deleteRecursively();
    }
    else
    {
        appendSetupLog (dir, "Uninstalled. Logs kept in this folder; configuration kept in " + paths::programDataDir().getFullPathName());
    }
    ::RemoveDirectoryW (dir.getFullPathName().toWideCharPointer()); // only when nothing was kept
    return true;
}

bool relaunchUninstallerFromTemp (const Options& options, const juce::File& dir)
{
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    const auto copy = temp.getChildFile ("Audioslave-uninstall-" + juce::String (static_cast<juce::int64> (::GetCurrentProcessId())) + ".exe");
    if (! self.copyFileTo (copy))
        return false;

    juce::String commandLine = win::quoteArgument (copy.getFullPathName()) + " /uninstall-stage2 "
                               + win::quoteArgument ("/dir=" + dir.getFullPathName());
    if (options.silent)
        commandLine << " /S";
    if (options.removeData)
        commandLine << " /removedata";

    std::wstring mutableLine (commandLine.toWideCharPointer());
    STARTUPINFOW startup {};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION info {};
    if (! ::CreateProcessW (copy.getFullPathName().toWideCharPointer(), mutableLine.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, temp.getFullPathName().toWideCharPointer(), &startup, &info))
    {
        copy.deleteFile();
        return false;
    }
    win::UniqueHandle process (info.hProcess), thread (info.hThread);
    if (options.silent)
        ::WaitForSingleObject (process.get(), INFINITE); // keep /S synchronous for scripts
    return true;
}

void deleteSelfLater (const juce::File& dir)
{
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const auto uninstaller = dir.getChildFile (brand::uninstallerExecutable);
    ::MoveFileExW (self.getFullPathName().toWideCharPointer(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    // Uninstall.exe may still be running (in silent mode it waits for this
    // copy to finish), so a single attempt can fail: retry for up to a minute
    // until both executables are gone, then remove the folder if it is empty.
    // Reboot-time deletion stays scheduled as a fallback.
    auto literal = [] (const juce::File& f) { return "'" + f.getFullPathName().replace ("'", "''") + "'"; };
    juce::String script;
    script << "for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Seconds 1; "
           << "Remove-Item -LiteralPath " << literal (self) << ", " << literal (uninstaller)
           << " -Force -ErrorAction SilentlyContinue; "
           << "if (-not (Test-Path -LiteralPath " << literal (uninstaller) << ") -and -not (Test-Path -LiteralPath "
           << literal (self) << ")) { break } }; "
           << "if (-not (Get-ChildItem -LiteralPath " << literal (dir) << " -Force -ErrorAction SilentlyContinue)) "
           << "{ Remove-Item -LiteralPath " << literal (dir) << " -Force -ErrorAction SilentlyContinue }";
    const auto command = "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command "
                         + win::quoteArgument (script);
    std::wstring mutableLine (command.toWideCharPointer());
    STARTUPINFOW startup {};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION info {};
    if (::CreateProcessW (nullptr, mutableLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
    {
        ::CloseHandle (info.hThread);
        ::CloseHandle (info.hProcess);
    }
}
} // namespace audioslave::setup
