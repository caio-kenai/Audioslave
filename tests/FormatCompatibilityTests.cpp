#include "config/Configuration.h"
#include "core/FormatCompatibility.h"
#include "core/FormatPolicy.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class FormatCompatibilityTests final : public juce::UnitTest
{
public:
    FormatCompatibilityTests() : juce::UnitTest ("Format compatibility", "Audio") {}

    static FormatCapabilities caps (std::initializer_list<std::pair<std::uint32_t, std::vector<std::uint16_t>>> entries)
    {
        FormatCapabilities c;
        c.known = true;
        for (const auto& [rate, depths] : entries)
            c.depthsByRate[rate] = depths;
        return c;
    }

    void runTest() override
    {
        beginTest ("Probe: every selectable rate and depth is asked, the answer is grouped per rate");
        {
            MockFormatStore store;
            // 16-bit packed, 24-in-32 and 32-bit float layouts.
            store.supported = { { 44100, 16, 16, false }, { 48000, 16, 16, false }, { 48000, 24, 32, false },
                                { 96000, 32, 32, true } };
            FormatPolicy policy (store);
            const auto c = policy.probe ("d", fmt (48000, 16, 16));
            expect (c.known);
            expect (c.rates() == std::vector<std::uint32_t> { 44100, 48000, 96000 });
            expect (c.depthsAt (48000) == std::vector<std::uint16_t> { 16, 24 });
            expect (c.depthsAt (44100) == std::vector<std::uint16_t> { 16 });
            expect (c.supports (96000, 32));
            expectEquals (static_cast<int> (c.maxDepthAt (48000)), 24);
            expectEquals (static_cast<int> (c.maxDepthAt (8000)), 0);
            expectEquals (c.describeRates(), juce::String ("44100, 48000, 96000 Hz"));
            expectEquals (c.describeDepths(), juce::String ("16, 24, 32-bit"));
        }

        beginTest ("Probe: every rate from 8000 to 384000 Hz at every depth is recognised");
        {
            MockFormatStore store;
            for (auto rate : supportedSampleRates)
            {
                store.supported.push_back ({ rate, 16, 16, false });
                store.supported.push_back ({ rate, 24, 24, false });
                store.supported.push_back ({ rate, 32, 32, false });
            }
            FormatPolicy policy (store);
            const auto c = policy.probe ("d", fmt (48000, 24, 24));
            expectEquals (static_cast<int> (c.depthsByRate.size()), static_cast<int> (supportedSampleRates.size()));
            for (auto rate : supportedSampleRates)
                for (auto bits : supportedBitDepths)
                {
                    expect (c.supports (rate, bits), juce::String (rate) + "/" + juce::String (bits));
                    expect (judgeCompatibility (c, rate, bits).isCompatible());
                }
        }

        beginTest ("Probe: a driver that cannot be asked gives unknown capabilities");
        {
            MockFormatStore store;
            store.supportResult = static_cast<ResultCode> (0x80070490); // E_NOTFOUND
            FormatPolicy policy (store);
            const auto c = policy.probe ("d", fmt (48000, 16, 16));
            expect (! c.known);
            expect (c.code == store.supportResult);
            expect (judgeCompatibility (c, 48000, 16).compatibility == Compatibility::unknown);
        }

        beginTest ("Compatible rate and bit depth");
        {
            const auto v = judgeCompatibility (caps ({ { 44100, { 16, 24 } }, { 48000, { 16, 24 } } }), 48000, 24);
            expect (v.compatibility == Compatibility::compatible);
            expect (v.isCompatible());
        }

        beginTest ("Incompatible sample rate");
        {
            const auto v = judgeCompatibility (caps ({ { 44100, { 16, 24 } }, { 48000, { 16, 24 } } }), 96000, 24);
            expect (v.compatibility == Compatibility::rateUnsupported);
            expect (v.reason (96000, 24).contains ("sample rate"));
        }

        beginTest ("Rate supported, bit depth not: distinct from a rate mismatch");
        {
            const auto v = judgeCompatibility (caps ({ { 48000, { 16 } } }), 48000, 24);
            expect (v.compatibility == Compatibility::depthUnsupported);
            expectEquals (static_cast<int> (v.maxDepthAtRate), 16);
            expect (v.depthsAtRate == std::vector<std::uint16_t> { 16 });
            expect (v.reason (48000, 24).contains ("bit depth"));
            expect (v.reason (48000, 24).contains ("16-bit"));
        }

        beginTest ("Compatible bit depth at the rate, even if other rates lack it");
        {
            const auto v = judgeCompatibility (caps ({ { 44100, { 16 } }, { 48000, { 16, 24 } } }), 48000, 24);
            expect (v.isCompatible());
        }

        beginTest ("Neither rate nor depth: reported as a rate mismatch (the rate is missing)");
        {
            const auto v = judgeCompatibility (caps ({ { 44100, { 16 } } }), 192000, 32);
            expect (v.compatibility == Compatibility::rateUnsupported);
        }

        beginTest ("No format reported at all (disconnected device): unknown, never incompatible");
        {
            FormatCapabilities none;
            none.known = true;
            expect (judgeCompatibility (none, 48000, 16).compatibility == Compatibility::unknown);
            expectEquals (none.describeRates(), juce::String ("none of the standard rates"));
        }

        beginTest ("Capabilities survive a save / load round trip");
        {
            const auto c = caps ({ { 44100, { 16, 24 } }, { 48000, { 16, 24, 32 } }, { 384000, { 32 } } });
            const auto text = c.serialise();
            expectEquals (text, juce::String ("44100:16/24;48000:16/24/32;384000:32"));
            expect (FormatCapabilities::deserialise (text) == c);

            FormatCapabilities none;
            none.known = true;
            expect (FormatCapabilities::deserialise (none.serialise()) == none);
            expect (! FormatCapabilities::deserialise ({}).known);
        }
    }
};

static FormatCompatibilityTests formatCompatibilityTests;
} // namespace audioslave::test
