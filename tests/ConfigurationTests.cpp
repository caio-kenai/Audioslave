#include "config/Configuration.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class ConfigurationTests final : public juce::UnitTest
{
public:
    ConfigurationTests() : juce::UnitTest ("Configuration", "Config") {}

    static ConfigurationLoadResult parse (const char* text) { return parseConfiguration (juce::String (text)); }

    void runTest() override
    {
        beginTest ("Defaults match the documented behaviour");
        {
            const Configuration cfg;
            expect (cfg.monitorPlayback);
            expect (cfg.monitorCapture);
            expectEquals (static_cast<int> (cfg.checkIntervalSeconds), 60);
            expect (cfg.enableLogging);
            expect (cfg.logLevel == LogLevel::info);
            expect (cfg.enforce);
            expect (cfg.exclusiveModeProtection);
            expect (! cfg.formatStandardization);
            expectEquals (static_cast<int> (cfg.sampleRate), 48000);
            expectEquals (static_cast<int> (cfg.bitDepth), 24);
        }

        beginTest ("Save / load round trip");
        {
            const auto file = tempFile ("config.ini");
            Configuration in;
            in.monitorPlayback = false;
            in.checkIntervalSeconds = 42;
            in.enableLogging = false;
            in.logLevel = LogLevel::debug;
            in.enforce = false;
            in.exclusiveModeProtection = false;
            in.formatStandardization = true;
            in.sampleRate = 96000;
            in.bitDepth = 16;
            expect (saveConfiguration (in, file).wasOk());
            const auto out = loadConfiguration (file, false);
            expect (out.fileFound);
            expect (out.warnings.isEmpty());
            expect (out.config == in);
            file.deleteFile();
        }

        beginTest ("Missing file: defaults, created on request");
        {
            const auto file = tempFile ("missing.ini");
            auto r = loadConfiguration (file, false);
            expect (! r.fileFound);
            expect (! file.existsAsFile());
            r = loadConfiguration (file, true);
            expect (file.existsAsFile());
            expect (r.config == Configuration {});
            file.deleteFile();
        }

        beginTest ("Unknown keys are ignored");
        {
            const auto r = parse ("[Monitor]\nPlayback=false\nBogusKey=hello\n");
            expect (! r.config.monitorPlayback);
            expectEquals (static_cast<int> (r.config.checkIntervalSeconds), 60);
        }

        beginTest ("Interval is clamped to 1..86400");
        {
            expectEquals (static_cast<int> (parse ("[Monitor]\nCheckIntervalSeconds=500000\n").config.checkIntervalSeconds), 86400);
            expectEquals (static_cast<int> (parse ("[Monitor]\nCheckIntervalSeconds=0\n").config.checkIntervalSeconds), 1);
            const auto bad = parse ("[Monitor]\nCheckIntervalSeconds=soon\n");
            expectEquals (static_cast<int> (bad.config.checkIntervalSeconds), 60);
            expectEquals (bad.warnings.size(), 1);
        }

        beginTest ("Log levels");
        {
            expect (parseLogLevel ("debug") == LogLevel::debug);
            expect (parseLogLevel ("INFO") == LogLevel::info);
            expect (parseLogLevel ("warning") == LogLevel::warn);
            expect (parseLogLevel ("error") == LogLevel::error);
            bool ok = true;
            expect (parseLogLevel ("bogus", LogLevel::info, &ok) == LogLevel::info);
            expect (! ok);
        }

        beginTest ("Every documented sample rate and bit depth is accepted");
        {
            for (auto rate : supportedSampleRates)
                for (auto bits : supportedBitDepths)
                {
                    const auto text = "[Features]\nSampleRate=" + juce::String (rate) + "\nBitDepth=" + juce::String (bits) + "\n";
                    const auto r = parseConfiguration (text);
                    expectEquals (static_cast<int> (r.config.sampleRate), static_cast<int> (rate));
                    expectEquals (static_cast<int> (r.config.bitDepth), static_cast<int> (bits));
                    expect (r.warnings.isEmpty());
                }
        }

        beginTest ("The full sample-rate range from 8000 to 384000 Hz is offered");
        {
            const std::uint32_t expected[] = { 8000,  11025, 12000,  16000,  22050,  24000,  32000, 44100,
                                               48000, 88200, 96000, 176400, 192000, 352800, 384000 };
            expectEquals (static_cast<int> (supportedSampleRates.size()), static_cast<int> (std::size (expected)));
            for (auto rate : expected)
                expect (isSupportedSampleRate (rate), juce::String (rate));
            for (std::uint64_t rate : { 0ull, 7999ull, 44000ull, 384001ull, 768000ull })
                expect (! isSupportedSampleRate (rate), juce::String (rate));
            for (std::uint64_t bits : { 16ull, 24ull, 32ull })
                expect (isSupportedBitDepth (bits));
            for (std::uint64_t bits : { 0ull, 8ull, 20ull, 64ull })
                expect (! isSupportedBitDepth (bits));
        }

        beginTest ("Unsupported sample rate / bit depth keep the default with a warning");
        {
            auto r = parse ("[Features]\nFormatStandardization=true\nSampleRate=22051\nBitDepth=32\n");
            expect (r.config.formatStandardization);
            expectEquals (static_cast<int> (r.config.sampleRate), 48000);
            expectEquals (static_cast<int> (r.config.bitDepth), 32);
            expectEquals (r.warnings.size(), 1);

            r = parse ("[Features]\nSampleRate=44100\nBitDepth=20\n");
            expectEquals (static_cast<int> (r.config.sampleRate), 44100);
            expectEquals (static_cast<int> (r.config.bitDepth), 24);
            expectEquals (r.warnings.size(), 1);
        }

        beginTest ("Invalid booleans keep the default with a warning");
        {
            const auto r = parse ("[Behavior]\nEnforce=maybe\n");
            expect (r.config.enforce);
            expectEquals (r.warnings.size(), 1);
        }

        beginTest ("Inline comments and case-insensitive sections / keys");
        {
            const auto r = parse ("[monitor]\nplayback=false   ; comment\nCheckIntervalSeconds=15 ; s\n");
            expect (! r.config.monitorPlayback);
            expectEquals (static_cast<int> (r.config.checkIntervalSeconds), 15);
        }

        beginTest ("A complete file written by hand (CRLF, comments) is read unchanged");
        {
            const auto r = parse ("; Audioslave configuration\r\n[Features]\r\nExclusiveModeProtection=true\r\n"
                                  "FormatStandardization=true\r\nSampleRate=44100\r\nBitDepth=16\r\n[Monitor]\r\n"
                                  "Playback=true\r\nCapture=false\r\nCheckIntervalSeconds=30\r\n[Logging]\r\nEnable=true\r\n"
                                  "Level=WARN\r\n[Behavior]\r\nEnforce=true\r\n");
            expect (r.warnings.isEmpty());
            expect (r.config.formatStandardization);
            expectEquals (static_cast<int> (r.config.sampleRate), 44100);
            expectEquals (static_cast<int> (r.config.bitDepth), 16);
            expect (! r.config.monitorCapture);
            expect (r.config.logLevel == LogLevel::warn);
        }

        beginTest ("Format target description");
        {
            Configuration cfg;
            cfg.sampleRate = 44100;
            cfg.bitDepth = 16;
            expectEquals (describeFormatTarget (cfg), juce::String ("44100 Hz / 16-bit"));
        }
    }
};

static ConfigurationTests configurationTests;
} // namespace audioslave::test
