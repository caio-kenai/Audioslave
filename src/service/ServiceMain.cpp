// AudioslaveService.exe entry point: Windows service (--service), portable
// monitoring host (--portable), console host (run) and command-line tools.

#include "platform/windows/WinCommon.h"
#include "cli/Commands.h"

#include <shellapi.h>

namespace
{
// Console output is UTF-8 while this process runs (Portuguese device names,
// paths); the previous code page is restored on exit.
struct ScopedUtf8Console
{
    ScopedUtf8Console() : previous (::GetConsoleOutputCP()) { ::SetConsoleOutputCP (CP_UTF8); }
    ~ScopedUtf8Console()
    {
        if (previous != 0)
            ::SetConsoleOutputCP (previous);
    }
    UINT previous;
};

// juce::ArgumentList from the wide command line (argv is ANSI on Windows).
juce::ArgumentList wideArguments()
{
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW (::GetCommandLineW(), &argc);
    juce::StringArray args;
    for (int i = 1; argv != nullptr && i < argc; ++i)
        args.add (juce::String (argv[i]));
    if (argv != nullptr)
        ::LocalFree (argv);
    return juce::ArgumentList ("AudioslaveService", args);
}
} // namespace

int main (int, char*[])
{
    const ScopedUtf8Console console;
    return audioslave::cli::run (wideArguments());
}
