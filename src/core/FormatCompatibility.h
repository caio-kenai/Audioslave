#pragma once
// What a device can really stream (asked from its driver), and how that
// compares with the format the user chose.
//
// The configuration offers every standard rate / depth (supportedSampleRates,
// supportedBitDepths); a device usually supports a subset. The verdict keeps
// the two kinds of mismatch apart, because they call for different decisions:
//   - rateUnsupported:  the device cannot run at the chosen sample rate at all;
//   - depthUnsupported: it runs at that rate, but not with the chosen bit
//                       depth (e.g. 48000 Hz only at 16-bit when 24 was chosen).

#include "audio/models/ResultCode.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <vector>

namespace audioslave
{
struct FormatCapabilities
{
    bool known = false;                 // false: the driver could not be asked
    ResultCode code = result::ok;       // why, when unknown
    // Supported bit depths per sample rate (only rates with at least one).
    std::map<std::uint32_t, std::vector<std::uint16_t>> depthsByRate;

    [[nodiscard]] bool supports (std::uint32_t rate, std::uint16_t bits) const;
    [[nodiscard]] bool supportsRate (std::uint32_t rate) const { return depthsByRate.count (rate) != 0; }
    [[nodiscard]] std::vector<std::uint32_t> rates() const;
    [[nodiscard]] std::vector<std::uint16_t> depthsAt (std::uint32_t rate) const;
    [[nodiscard]] std::uint16_t maxDepthAt (std::uint32_t rate) const; // 0 when the rate is unsupported
    [[nodiscard]] std::vector<std::uint16_t> allDepths() const;

    // "44100, 48000 Hz" / "16, 24-bit"
    [[nodiscard]] juce::String describeRates() const;
    [[nodiscard]] juce::String describeDepths() const;

    // Compact persistent form: "44100:16/24;48000:16/24" (empty when unknown).
    [[nodiscard]] juce::String serialise() const;
    static FormatCapabilities deserialise (const juce::String& text);

    bool operator== (const FormatCapabilities&) const = default;
};

enum class Compatibility
{
    compatible,
    rateUnsupported,
    depthUnsupported,
    unknown // not asked, or no format reported at all (e.g. disconnected)
};

juce::String compatibilityName (Compatibility c); // "compatible", "rate-unsupported", ...

struct CompatibilityVerdict
{
    Compatibility compatibility = Compatibility::unknown;
    std::vector<std::uint16_t> depthsAtRate; // depthUnsupported: what the rate offers
    std::uint16_t maxDepthAtRate = 0;

    [[nodiscard]] bool isCompatible() const noexcept { return compatibility == Compatibility::compatible; }
    // English, for the log: "requested bit depth is not supported (48000 Hz supports up to 16-bit)".
    [[nodiscard]] juce::String reason (std::uint32_t rate, std::uint16_t bits) const;
};

CompatibilityVerdict judgeCompatibility (const FormatCapabilities& caps, std::uint32_t rate, std::uint16_t bits);
} // namespace audioslave
