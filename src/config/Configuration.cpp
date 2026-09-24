#include "config/Configuration.h"

#include <algorithm>

namespace audioslave
{
namespace
{
constexpr std::uint64_t maxIntervalSeconds = 24ull * 3600ull;

bool parseBool (const juce::String& value, bool& out)
{
    const auto v = value.toLowerCase();
    if (v == "true" || v == "1" || v == "yes" || v == "on")
    {
        out = true;
        return true;
    }
    if (v == "false" || v == "0" || v == "no" || v == "off")
    {
        out = false;
        return true;
    }
    return false;
}

bool parseUnsigned (const juce::String& value, std::uint64_t& out)
{
    if (value.isEmpty() || ! value.containsOnly ("0123456789") || value.length() > 18)
        return false;
    out = static_cast<std::uint64_t> (value.getLargeIntValue());
    return true;
}

juce::String joinRates()
{
    juce::StringArray items;
    for (auto r : supportedSampleRates)
        items.add (juce::String (r));
    return items.joinIntoString (", ");
}

juce::String joinDepths()
{
    juce::StringArray items;
    for (auto d : supportedBitDepths)
        items.add (juce::String (d));
    return items.joinIntoString (", ");
}
} // namespace

bool isSupportedSampleRate (std::uint64_t rate) noexcept
{
    return std::find (supportedSampleRates.begin(), supportedSampleRates.end(), rate) != supportedSampleRates.end();
}

bool isSupportedBitDepth (std::uint64_t bits) noexcept
{
    return std::find (supportedBitDepths.begin(), supportedBitDepths.end(), bits) != supportedBitDepths.end();
}

juce::String describeFormatTarget (const Configuration& config)
{
    return juce::String (config.sampleRate) + " Hz / " + juce::String (config.bitDepth) + "-bit";
}

juce::String formatTargetKey (const Configuration& config)
{
    return juce::String (config.sampleRate) + ":" + juce::String (config.bitDepth);
}

bool disablePolicyConfirmed (const Configuration& config)
{
    return config.formatStandardization && config.disableIncompatibleDevices
           && config.disableConfirmedFor == formatTargetKey (config);
}

juce::String customDeviceName (const Configuration& config, const juce::String& endpointId)
{
    for (const auto& [id, name] : config.deviceNames)
        if (id.equalsIgnoreCase (endpointId))
            return name;
    return {};
}

ConfigurationLoadResult parseConfiguration (const juce::String& text)
{
    ConfigurationLoadResult result;
    result.fileFound = true;
    auto& cfg = result.config;

    auto warnBool = [&result] (const juce::String& key, const juce::String& value, bool fallback)
    {
        result.warnings.add ("config: " + key + "=" + value + " is not a boolean; using " + (fallback ? "true" : "false") + ".");
    };

    juce::String section;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar (';') || line.startsWithChar ('#'))
            continue;
        if (line.startsWithChar ('[') && line.endsWithChar (']'))
        {
            section = line.substring (1, line.length() - 1).trim().toLowerCase();
            continue;
        }
        const int eq = line.indexOfChar ('=');
        if (eq < 0)
            continue;

        const auto rawKey = line.substring (0, eq).trim();
        auto value = line.substring (eq + 1).trim();
        if (section == "devicenames")
        {
            // Names are free text (';' included); ids are kept as written.
            if (rawKey.isNotEmpty() && value.isNotEmpty())
                cfg.deviceNames[rawKey] = value;
            continue;
        }
        // Trailing inline comments: "Enforce=true ; comment".
        if (const int sc = value.indexOfChar (';'); sc >= 0)
            value = value.substring (0, sc).trim();
        const auto key = section + "." + rawKey.toLowerCase();

        auto readBool = [&] (bool& field)
        {
            bool parsed = field;
            if (parseBool (value, parsed))
                field = parsed;
            else
                warnBool (rawKey, value, field);
        };

        std::uint64_t n = 0;
        if (key == "monitor.playback")
            readBool (cfg.monitorPlayback);
        else if (key == "monitor.capture")
            readBool (cfg.monitorCapture);
        else if (key == "monitor.checkintervalseconds")
        {
            if (parseUnsigned (value, n))
                cfg.checkIntervalSeconds = static_cast<std::uint32_t> (std::clamp<std::uint64_t> (n, 1, maxIntervalSeconds));
            else
                result.warnings.add ("config: CheckIntervalSeconds=" + value + " is not a number; using "
                                     + juce::String (cfg.checkIntervalSeconds) + ".");
        }
        else if (key == "logging.enable")
            readBool (cfg.enableLogging);
        else if (key == "logging.level")
        {
            bool ok = true;
            cfg.logLevel = parseLogLevel (value, LogLevel::info, &ok);
            if (! ok)
                result.warnings.add ("config: Level=" + value + " is not DEBUG, INFO, WARN or ERROR; using INFO.");
        }
        else if (key == "behavior.enforce")
            readBool (cfg.enforce);
        else if (key == "features.exclusivemodeprotection")
            readBool (cfg.exclusiveModeProtection);
        else if (key == "features.formatstandardization")
            readBool (cfg.formatStandardization);
        else if (key == "features.samplerate")
        {
            if (parseUnsigned (value, n) && isSupportedSampleRate (n))
                cfg.sampleRate = static_cast<std::uint32_t> (n);
            else
                result.warnings.add ("config: SampleRate=" + value + " is not supported (use " + joinRates()
                                     + "); using " + juce::String (cfg.sampleRate) + ".");
        }
        else if (key == "features.disableincompatibledevices")
            readBool (cfg.disableIncompatibleDevices);
        else if (key == "features.disableconfirmedfor")
            cfg.disableConfirmedFor = value;
        else if (key == "features.bitdepth")
        {
            if (parseUnsigned (value, n) && isSupportedBitDepth (n))
                cfg.bitDepth = static_cast<std::uint16_t> (n);
            else
                result.warnings.add ("config: BitDepth=" + value + " is not supported (use " + joinDepths()
                                     + "); using " + juce::String (cfg.bitDepth) + ".");
        }
    }

    return result;
}

ConfigurationLoadResult loadConfiguration (const juce::File& file, bool createWhenMissing)
{
    if (! file.existsAsFile())
    {
        ConfigurationLoadResult result;
        if (createWhenMissing)
        {
            if (auto saved = saveConfiguration (result.config, file); saved.failed())
                result.warnings.add ("config: could not create " + file.getFullPathName() + ": " + saved.getErrorMessage());
        }
        return result;
    }
    return parseConfiguration (file.loadFileAsString());
}

juce::Result saveConfiguration (const Configuration& cfg, const juce::File& file)
{
    const juce::String nl ("\r\n");
    juce::String out;
    out << "; Audioslave configuration" << nl
        << "; Changes are applied when the service restarts, when monitoring is resumed," << nl
        << "; or with: sc control Audioslave paramchange" << nl
        << nl
        << "[Features]" << nl
        << "; Keep \"Allow applications to take exclusive control\" disabled (main feature)." << nl
        << "ExclusiveModeProtection=" << (cfg.exclusiveModeProtection ? "true" : "false") << nl
        << "; Keep every endpoint's default format at SampleRate/BitDepth when the device supports it." << nl
        << "FormatStandardization=" << (cfg.formatStandardization ? "true" : "false") << nl
        << "; " << joinRates().replace (", ", " | ") << nl
        << "SampleRate=" << juce::String (cfg.sampleRate) << nl
        << "; " << joinDepths().replace (", ", " | ") << nl
        << "BitDepth=" << juce::String (cfg.bitDepth) << nl
        << "; Disable the devices that do not support SampleRate/BitDepth (false = only report them)." << nl
        << "DisableIncompatibleDevices=" << (cfg.disableIncompatibleDevices ? "true" : "false") << nl
        << "; Written when the user confirms the policy for a format (rate:bits); do not edit." << nl
        << "DisableConfirmedFor=" << cfg.disableConfirmedFor << nl
        << nl
        << "[Monitor]" << nl
        << "Playback=" << (cfg.monitorPlayback ? "true" : "false") << nl
        << "Capture=" << (cfg.monitorCapture ? "true" : "false") << nl
        << "; Periodic safety-net check in seconds (1..86400)." << nl
        << "CheckIntervalSeconds=" << juce::String (cfg.checkIntervalSeconds) << nl
        << nl
        << "[Logging]" << nl
        << "Enable=" << (cfg.enableLogging ? "true" : "false") << nl
        << "; DEBUG | INFO | WARN | ERROR" << nl
        << "Level=" << logLevelName (cfg.logLevel) << nl
        << nl
        << "[Behavior]" << nl
        << "; false = report only, never modify any device." << nl
        << "Enforce=" << (cfg.enforce ? "true" : "false") << nl;

    if (! cfg.deviceNames.empty())
    {
        out << nl << "[DeviceNames]" << nl
            << "; Names chosen in Audioslave (endpoint id = name); restored if Windows or a driver resets them." << nl;
        for (const auto& [id, name] : cfg.deviceNames)
            out << id << "=" << name.replaceCharacters ("\r\n", "  ").trim() << nl;
    }

    if (auto dir = file.getParentDirectory(); ! dir.isDirectory())
        if (auto created = dir.createDirectory(); created.failed())
            return created;

    if (! file.replaceWithText (out, false, false, nullptr))
        return juce::Result::fail ("cannot write " + file.getFullPathName());
    return juce::Result::ok();
}
} // namespace audioslave
