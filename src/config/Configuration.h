#pragma once
// config.ini (C:\ProgramData\Audioslave\config.ini).
//
// The file stays a plain, hand-editable INI with the same sections and keys
// as Audio Watchdog, so an existing Audio Watchdog configuration can be
// imported as-is. Unknown keys are ignored; invalid values keep their
// defaults and produce a warning (returned to the caller, which logs it once
// logging is configured).

#include "logging/Logger.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace audioslave
{
inline constexpr std::array<std::uint32_t, 6> supportedSampleRates { 44100, 48000, 88200, 96000, 176400, 192000 };
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

// Parses INI text (exposed for tests and the legacy-config import).
ConfigurationLoadResult parseConfiguration (const juce::String& text);

// Writes the whole configuration with comments (UTF-8, CRLF).
juce::Result saveConfiguration (const Configuration& config, const juce::File& file);

// "48000 Hz / 24-bit"
juce::String describeFormatTarget (const Configuration& config);
} // namespace audioslave
