#include "core/FormatPolicy.h"
#include "config/Configuration.h"

#include <optional>

namespace audioslave
{
juce::String formatResultName (FormatResult r)
{
    switch (r)
    {
        case FormatResult::compliant:    return "compliant";
        case FormatResult::applied:      return "applied";
        case FormatResult::unsupported:  return "unsupported";
        case FormatResult::unknown:      return "unknown";
        case FormatResult::writeFailed:  return "write-failed";
        case FormatResult::verifyFailed: return "verify-failed";
        case FormatResult::skipped:      return "skipped";
    }
    return "unknown";
}

std::vector<AudioFormat> FormatPolicy::candidates (std::uint32_t sampleRate, std::uint16_t bitDepth,
                                                   std::uint16_t channels, std::uint32_t channelMask)
{
    AudioFormat base;
    base.sampleRate = sampleRate;
    base.validBits = bitDepth;
    base.channels = channels == 0 ? 2 : channels;
    base.channelMask = channelMask;

    std::vector<AudioFormat> out;
    auto add = [&] (std::uint16_t container, bool isFloat)
    {
        AudioFormat f = base;
        f.containerBits = container;
        f.isFloat = isFloat;
        out.push_back (f);
    };
    switch (bitDepth)
    {
        case 16: add (16, false); break;
        case 24: add (24, false); add (32, false); break;
        case 32: add (32, false); add (32, true); break;
        default: break;
    }
    return out;
}

juce::String FormatPolicy::describeSupported (const juce::String& endpointId, const AudioFormat& current)
{
    juce::StringArray list;
    for (auto rate : supportedSampleRates)
    {
        for (auto depth : supportedBitDepths)
        {
            for (const auto& f : candidates (rate, depth, current.channels, current.channelMask))
            {
                bool ok = false;
                if (failed (store_.isFormatSupported (endpointId, f, ok)))
                    return "(could not be determined)";
                if (ok)
                {
                    list.add (describeFormat (f));
                    break;
                }
            }
        }
    }
    return list.isEmpty() ? juce::String ("(none of the standard PCM formats)") : list.joinIntoString (", ");
}

FormatOutcome FormatPolicy::judgeAndApply (const juce::String& endpointId, std::uint32_t sampleRate,
                                           std::uint16_t bitDepth, bool enforce)
{
    FormatOutcome out;
    out.code = store_.getDeviceFormat (endpointId, out.before);
    if (failed (out.code))
    {
        out.result = FormatResult::unknown;
        return out;
    }
    if (out.before.matches (sampleRate, bitDepth))
    {
        out.result = FormatResult::compliant;
        return out;
    }

    // Ask the driver before touching anything.
    std::optional<AudioFormat> chosen;
    for (const auto& f : candidates (sampleRate, bitDepth, out.before.channels, out.before.channelMask))
    {
        bool ok = false;
        if (const auto rc = store_.isFormatSupported (endpointId, f, ok); failed (rc))
        {
            out.code = rc;
            out.result = FormatResult::unknown;
            return out;
        }
        if (ok)
        {
            chosen = f;
            break;
        }
    }
    if (! chosen)
    {
        out.result = FormatResult::unsupported;
        out.supported = describeSupported (endpointId, out.before);
        return out;
    }

    out.applied = *chosen;
    if (! enforce)
    {
        out.result = FormatResult::skipped;
        return out;
    }

    out.code = store_.setDeviceFormat (endpointId, *chosen);
    if (failed (out.code))
    {
        out.result = FormatResult::writeFailed;
        return out;
    }

    AudioFormat after;
    if (failed (store_.getDeviceFormat (endpointId, after)) || ! after.matches (sampleRate, bitDepth))
    {
        out.result = FormatResult::verifyFailed;
        return out;
    }
    out.result = FormatResult::applied;
    return out;
}
} // namespace audioslave
