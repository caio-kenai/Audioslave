#pragma once
// PCM / float layout of an endpoint's shared-mode "Default Format".

#include "audio/models/AudioEndpoint.h"

#include <cstdint>

namespace audioslave
{
struct AudioFormat
{
    std::uint32_t sampleRate = 0;
    std::uint16_t validBits = 0;      // bit depth shown in the Sound panel
    std::uint16_t containerBits = 0;  // storage per sample (24-bit may live in 32)
    bool isFloat = false;
    std::uint16_t channels = 2;
    std::uint32_t channelMask = 0x3;  // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT

    [[nodiscard]] bool matches (std::uint32_t rate, std::uint16_t bits) const noexcept
    {
        return sampleRate == rate && validBits == bits;
    }

    bool operator== (const AudioFormat&) const = default;
};

// "48000 Hz / 24-bit"
juce::String describeFormat (const AudioFormat& format);
} // namespace audioslave
