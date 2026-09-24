#pragma once
// config.ini (C:\ProgramData\Audioslave\config.ini).
//
// A plain, hand-editable INI. Unknown keys are ignored; invalid values keep
// their defaults and produce a warning (returned to the caller, which logs
// it once logging is configured).

#include "logging/Logger.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace audioslave
{
// Every rate the Windows Sound panel / WASAPI offers for PCM endpoints. These
// are the choices the user can make; what each device really supports is
// asked from its driver (FormatPolicy::probe) and never assumed.
inline constexpr std::array<std::uint32_t, 15> supportedSampleRates { 8000,  11025, 12000,  16000,  22050,  24000,  32000, 44100,
                                                                      48000, 88200, 96000, 176400, 192000, 352800, 384000 };
inline constexpr std::array<std::uint16_t, 3> supportedBitDepths { 16, 24, 32 };
inline constexpr std::uint32_t defaultSampleRate = 48000;
inline constexpr std::uint16_t defaultBitDepth = 24;

bool isSupportedSampleRate (std::uint64_t rate) noexcept;
bool isSupportedBitDepth (std::uint64_t bits) noexcept;

struct Configuration
{
    // [Monitor]
    bool monitorPlayback = true;
    bool monitorCapture = true;
    std::uint32_t checkIntervalSeconds = 60; // periodic safety-net scan, 1..86400

    // [Logging]
    bool enableLogging = true;
    LogLevel logLevel = LogLevel::info;

    // [Behavior] false = report only, never modify a device.
    bool enforce = true;

    // [Features]
    bool exclusiveModeProtection = true;
    bool formatStandardization = false;
    std::uint32_t sampleRate = defaultSampleRate;
    std::uint16_t bitDepth = defaultBitDepth;

    bool operator== (const Configuration&) const = default;
};

struct ConfigurationLoadResult
{
    Configuration config;
    juce::StringArray warnings;
    bool fileFound = false;
};

// Loads `file`. When it is missing the defaults are returned and, if
// `createWhenMissing`, written to disk.
ConfigurationLoadResult loadConfiguration (const juce::File& file, bool createWhenMissing);

// Parses INI text (exposed for tests).
ConfigurationLoadResult parseConfiguration (const juce::String& text);

// Writes the whole configuration with comments (UTF-8, CRLF).
juce::Result saveConfiguration (const Configuration& config, const juce::File& file);

// "48000 Hz / 24-bit"
juce::String describeFormatTarget (const Configuration& config);
} // namespace audioslave
