#include "core/FormatCompatibility.h"

#include <algorithm>

namespace audioslave
{
bool FormatCapabilities::supports (std::uint32_t rate, std::uint16_t bits) const
{
    const auto it = depthsByRate.find (rate);
    return it != depthsByRate.end() && std::find (it->second.begin(), it->second.end(), bits) != it->second.end();
}

std::vector<std::uint32_t> FormatCapabilities::rates() const
{
    std::vector<std::uint32_t> out;
    for (const auto& [rate, depths] : depthsByRate)
        out.push_back (rate);
    return out;
}

std::vector<std::uint16_t> FormatCapabilities::depthsAt (std::uint32_t rate) const
{
    const auto it = depthsByRate.find (rate);
    return it != depthsByRate.end() ? it->second : std::vector<std::uint16_t> {};
}

std::uint16_t FormatCapabilities::maxDepthAt (std::uint32_t rate) const
{
    const auto depths = depthsAt (rate);
    return depths.empty() ? std::uint16_t (0) : *std::max_element (depths.begin(), depths.end());
}

std::vector<std::uint16_t> FormatCapabilities::allDepths() const
{
    std::vector<std::uint16_t> out;
    for (const auto& [rate, depths] : depthsByRate)
        for (auto d : depths)
            if (std::find (out.begin(), out.end(), d) == out.end())
                out.push_back (d);
    std::sort (out.begin(), out.end());
    return out;
}

juce::String FormatCapabilities::describeRates() const
{
    if (! known)
        return "unknown";
    if (depthsByRate.empty())
        return "none of the standard rates";
    juce::StringArray items;
    for (auto r : rates())
        items.add (juce::String (r));
    return items.joinIntoString (", ") + " Hz";
}

juce::String FormatCapabilities::describeDepths() const
{
    if (! known)
        return "unknown";
    const auto depths = allDepths();
    if (depths.empty())
        return "none";
    juce::StringArray items;
    for (auto d : depths)
        items.add (juce::String (d));
    return items.joinIntoString (", ") + "-bit";
}

juce::String FormatCapabilities::serialise() const
{
    if (! known)
        return {};
    juce::StringArray entries;
    for (const auto& [rate, depths] : depthsByRate)
    {
        juce::StringArray d;
        for (auto b : depths)
            d.add (juce::String (b));
        entries.add (juce::String (rate) + ":" + d.joinIntoString ("/"));
    }
    // "-" marks "asked, supports none of them" (distinct from unknown).
    return entries.isEmpty() ? juce::String ("-") : entries.joinIntoString (";");
}

FormatCapabilities FormatCapabilities::deserialise (const juce::String& text)
{
    FormatCapabilities caps;
    const auto t = text.trim();
    if (t.isEmpty())
        return caps;
    caps.known = true;
    if (t == "-")
        return caps;
    for (const auto& entry : juce::StringArray::fromTokens (t, ";", {}))
    {
        const auto rate = entry.upToFirstOccurrenceOf (":", false, false).trim().getLargeIntValue();
        if (rate <= 0)
            continue;
        std::vector<std::uint16_t> depths;
        for (const auto& d : juce::StringArray::fromTokens (entry.fromFirstOccurrenceOf (":", false, false), "/", {}))
            if (const auto bits = d.trim().getIntValue(); bits > 0)
                depths.push_back (static_cast<std::uint16_t> (bits));
        std::sort (depths.begin(), depths.end());
        if (! depths.empty())
            caps.depthsByRate[static_cast<std::uint32_t> (rate)] = depths;
    }
    return caps;
}

juce::String compatibilityName (Compatibility c)
{
    switch (c)
    {
        case Compatibility::compatible:       return "compatible";
        case Compatibility::rateUnsupported:  return "rate-unsupported";
        case Compatibility::depthUnsupported: return "depth-unsupported";
        case Compatibility::unknown:          break;
    }
    return "unknown";
}

juce::String CompatibilityVerdict::reason (std::uint32_t rate, std::uint16_t bits) const
{
    switch (compatibility)
    {
        case Compatibility::compatible:
            return "supported";
        case Compatibility::rateUnsupported:
            return "requested sample rate is not supported (" + juce::String (rate) + " Hz not available)";
        case Compatibility::depthUnsupported:
        {
            juce::StringArray d;
            for (auto b : depthsAtRate)
                d.add (juce::String (b));
            return "requested bit depth is not supported (" + juce::String (rate) + " Hz is available only at "
                   + d.joinIntoString (", ") + "-bit, not " + juce::String (bits) + "-bit)";
        }
        case Compatibility::unknown:
            break;
    }
    return "the supported formats could not be determined";
}

CompatibilityVerdict judgeCompatibility (const FormatCapabilities& caps, std::uint32_t rate, std::uint16_t bits)
{
    CompatibilityVerdict v;
    if (! caps.known)
        return v;
    if (! caps.supportsRate (rate))
    {
        v.compatibility = Compatibility::rateUnsupported;
        return v;
    }
    v.depthsAtRate = caps.depthsAt (rate);
    v.maxDepthAtRate = caps.maxDepthAt (rate);
    v.compatibility = caps.supports (rate, bits) ? Compatibility::compatible : Compatibility::depthUnsupported;
    return v;
}
} // namespace audioslave
