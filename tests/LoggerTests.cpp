#include "logging/Logger.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class LoggerTests final : public juce::UnitTest
{
public:
    LoggerTests() : juce::UnitTest ("Logger", "Logging") {}

    void runTest() override
    {
        auto& log = Logger::instance();
        const auto previousLevel = log.getLevel();

        beginTest ("Writes the file with level filtering");
        {
            const auto file = tempFile ("log.log");
            log.configure (file, LogLevel::warn, false);
            log.info ("should not appear");
            log.warn ("warning appears");
            log.error (juce::String (juce::CharPointer_UTF8 ("erro com acentuação")));
            log.close();

            const auto content = file.loadFileAsString();
            expect (! content.contains ("should not appear"));
            expect (content.contains ("[WARN]"));
            expect (content.contains ("warning appears"));
            expect (content.contains (juce::String (juce::CharPointer_UTF8 ("erro com acentuação"))));
            file.deleteFile();
        }

        beginTest ("JUCE's own log messages reach the same file");
        {
            const auto file = tempFile ("juce.log");
            log.configure (file, LogLevel::info, false);
            juce::Logger::writeToLog ("message from JUCE");
            log.close();
            expect (file.loadFileAsString().contains ("[JUCE] message from JUCE"));
            file.deleteFile();
        }

        beginTest ("Rotation keeps at most five old copies");
        {
            const auto dir = tempFile ("logs");
            dir.createDirectory();
            const auto file = dir.getChildFile ("audioslave.log");
            log.setMaxFileBytes (1024);
            log.configure (file, LogLevel::info, false);
            for (int i = 0; i < 400; ++i)
                log.info ("line " + juce::String (i) + " " + juce::String::repeatedString ("x", 60));
            log.close();
            log.setMaxFileBytes (Logger::defaultMaxFileBytes);

            expect (file.existsAsFile());
            expect (juce::File (file.getFullPathName() + ".1").existsAsFile());
            expect (juce::File (file.getFullPathName() + ".5").existsAsFile());
            expect (! juce::File (file.getFullPathName() + ".6").existsAsFile());
            expect (file.getSize() < 4096);
            dir.deleteRecursively();
        }

        beginTest ("Level names and line format");
        {
            expectEquals (logLevelName (LogLevel::debug), juce::String ("DEBUG"));
            expectEquals (logLevelName (LogLevel::info), juce::String ("INFO"));
            expectEquals (logLevelName (LogLevel::warn), juce::String ("WARN"));
            expectEquals (logLevelName (LogLevel::error), juce::String ("ERROR"));
            const auto line = Logger::formatLine (LogLevel::info, "hello");
            expect (line.startsWith ("["));
            expect (line.contains ("] [INFO] [pid "));
            expect (line.endsWith ("] hello"));
        }

        log.setLevel (previousLevel);
    }
};

static LoggerTests loggerTests;
} // namespace audioslave::test
