// Audioslave.exe - the single Audioslave executable:
//
//   Audioslave.exe                 system-tray application (JUCE GUI)
//   Audioslave.exe --service       Windows service (started by the SCM)
//   Audioslave.exe <command> ...   command-line tools (install, status, pause, ...)
//
// JUCE's START_JUCE_APPLICATION would own WinMain; this WinMain routes the
// command line first and only then hands over to JUCEApplicationBase::main.

#include "platform/windows/WinCommon.h"
#include "app/AudioslaveApplication.h"
#include "cli/Commands.h"

#include <shellapi.h>

#include <cstdio>
#include <iostream>

namespace
{
juce::JUCEApplicationBase* createApplication()
{
    return new audioslave::AudioslaveApplication();
}

juce::StringArray commandLineArguments()
{
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW (::GetCommandLineW(), &argc);
    juce::StringArray args;
    for (int i = 1; argv != nullptr && i < argc; ++i)
        args.add (juce::String (argv[i]));
    if (argv != nullptr)
        ::LocalFree (argv);
    return args;
}

// A GUI-subsystem executable has no console: attach to the one it was started
// from (cmd / PowerShell), or open one (double-click, elevated relaunch).
// Output that is already redirected (pipe / file) is left alone.
void attachConsole()
{
    const HANDLE out = ::GetStdHandle (STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE)
        return;

    if (! ::AttachConsole (ATTACH_PARENT_PROCESS))
        ::AllocConsole();
    FILE* stream = nullptr;
    ::freopen_s (&stream, "CONOUT$", "w", stdout);
    ::freopen_s (&stream, "CONOUT$", "w", stderr);
    ::freopen_s (&stream, "CONIN$", "r", stdin);
    std::cout.clear();
    std::cerr.clear();
    ::SetConsoleOutputCP (CP_UTF8);
    // The prompt was printed before our output; start on a fresh line.
    std::fputs ("\n", stdout);
}
} // namespace

int WINAPI WinMain (HINSTANCE, HINSTANCE, LPSTR, int)
{
    const auto args = commandLineArguments();
    if (args.isEmpty())
    {
        juce::JUCEApplicationBase::createInstance = &createApplication;
        return juce::JUCEApplicationBase::main();
    }

    // The service runs without any console.
    if (args[0] != "--service")
        attachConsole();
    return audioslave::cli::run (juce::ArgumentList ("Audioslave", args));
}
