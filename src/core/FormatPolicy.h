#pragma once
// Audio Format Standardization policy: compare an endpoint's default format
// with the configured target and apply it only when the driver reports that
// it supports it. Never falls back to a different format.

#include "audio/AudioInterfaces.h"
#include "core/FormatCompatibility.h"

#include <cstdint>
#include <vector>

namespace audioslave
{
enum class FormatResult
{
    compliant,    // already at the target rate / bit depth
    applied,      // changed to the target and verified
    unsupported,  // the device does not support the target -> skipped
    unknown,      // current or supported formats could not be determined
    writeFailed,  // the change was rejected
    verifyFailed, // the change was accepted but did not stick
    skipped       // not compliant, but not changed (enforcement off / backoff / paused)
};

juce::String formatResultName (FormatResult r);

struct FormatOutcome
{
    FormatResult result = FormatResult::unknown;
    AudioFormat before;          // format found on the device
    AudioFormat applied;         // layout written (applied / writeFailed / verifyFailed)
    ResultCode code = result::ok; // failure detail (unknown / writeFailed)
};

class FormatPolicy
{
public:
    explicit FormatPolicy (IAudioFormatStore& store) : store_ (store) {}

    FormatOutcome judgeAndApply (const juce::String& endpointId, std::uint32_t sampleRate, std::uint16_t bitDepth,
                                 bool enforce);

    // Concrete layouts that realise a bit depth, in preference order
    // (24-bit may be packed 24/24 or 24-in-32 depending on the driver;
    // 32-bit may be integer or float).
    static std::vector<AudioFormat> candidates (std::uint32_t sampleRate, std::uint16_t bitDepth,
                                                std::uint16_t channels, std::uint32_t channelMask);

    // Asks the driver about every selectable rate x depth (for the layout of
    // `current`: channel count and mask).
    FormatCapabilities probe (const juce::String& endpointId, const AudioFormat& current);

    // The standard rates / depths the device supports (for logs and `devices`).
    juce::String describeSupported (const juce::String& endpointId, const AudioFormat& current);

    // Read-only: the endpoint's current default format.
    ResultCode currentFormat (const juce::String& endpointId, AudioFormat& out) { return store_.getDeviceFormat (endpointId, out); }

private:
    IAudioFormatStore& store_;
};
} // namespace audioslave
