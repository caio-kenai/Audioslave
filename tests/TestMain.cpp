// audioslave_tests: juce::UnitTest runner.
//
//   audioslave_tests                 unit tests (no device is touched)
//   audioslave_tests --integration   also the read-only Windows integration tests
//   audioslave_tests --category IPC  one category only

#include "platform/windows/WinCommon.h"

#include <juce_events/juce_events.h>

#include <shellapi.h>

#include <cstdio>

namespace
{
class ConsoleRunner final : public juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        std::fputs ((message + "\n").toRawUTF8(), stdout);
        std::fflush (stdout);
    }
};

juce::StringArray wideArguments()
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
} // namespace

int main (int, char*[])
{
    ::SetConsoleOutputCP (CP_UTF8);
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::ArgumentList args ("audioslave_tests", wideArguments());
    const bool integration = args.containsOption ("--integration");
    const auto category = args.getValueForOption ("--category");

    juce::Array<juce::UnitTest*> selected;
    for (auto* test : juce::UnitTest::getAllTests())
    {
        if (category.isNotEmpty() && test->getCategory() != category)
            continue;
        if (test->getCategory() == "Integration" && ! integration && category != "Integration")
            continue;
        selected.add (test);
    }

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);
    runner.runTests (selected);

    int passes = 0, failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        passes += runner.getResult (i)->passes;
        failures += runner.getResult (i)->failures;
    }
    std::printf ("\n%d test(s), %d check(s) passed, %d failed\n", selected.size(), passes, failures);
    return failures == 0 ? 0 : 1;
}
