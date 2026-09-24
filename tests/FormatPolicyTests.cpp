#include "core/FormatPolicy.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class FormatPolicyTests final : public juce::UnitTest
{
public:
    FormatPolicyTests() : juce::UnitTest ("Format policy", "Audio") {}

    void runTest() override
    {
        beginTest ("Already compliant (24-in-32 counts as 24-bit)");
        {
            MockFormatStore store;
            store.current["d"] = fmt (48000, 24, 32);
            FormatPolicy policy (store);
            expect (policy.judgeAndApply ("d", 48000, 24, true).result == FormatResult::compliant);
            expectEquals (store.writeCalls.load(), 0);
        }

        beginTest ("Applies a supported format and verifies it");
        {
            MockFormatStore store;
            store.current["d"] = fmt (44100, 16, 16);
            store.supported = { { 44100, 16, 16, false }, { 48000, 24, 24, false }, { 96000, 24, 24, false } };
            FormatPolicy policy (store);
            const auto out = policy.judgeAndApply ("d", 48000, 24, true);
            expect (out.result == FormatResult::applied);
            expectEquals (store.writeCalls.load(), 1);
            expectEquals (static_cast<int> (store.current["d"].sampleRate), 48000);
            expectEquals (static_cast<int> (store.current["d"].validBits), 24);
            expectEquals (describeFormat (out.before), juce::String ("44100 Hz / 16-bit"));
        }

        beginTest ("High rates (96 / 192 kHz) are applied when supported");
        {
            MockFormatStore store;
            store.current["d"] = fmt (48000, 24, 24);
            store.supported = { { 192000, 32, 32, false } };
            FormatPolicy policy (store);
            expect (policy.judgeAndApply ("d", 192000, 32, true).result == FormatResult::applied);
            expectEquals (static_cast<int> (store.lastWritten.sampleRate), 192000);
        }

        beginTest ("Uses 24-in-32 when packed 24-bit is not supported");
        {
            MockFormatStore store;
            store.current["d"] = fmt (44100, 16, 16);
            store.supported = { { 48000, 16, 16, false }, { 48000, 24, 32, false } };
            FormatPolicy policy (store);
            expect (policy.judgeAndApply ("d", 48000, 24, true).result == FormatResult::applied);
            expectEquals (static_cast<int> (store.lastWritten.containerBits), 32);
            expectEquals (static_cast<int> (store.lastWritten.validBits), 24);
        }

        beginTest ("Unsupported target: skipped, never substituted");
        {
            MockFormatStore store;
            store.current["d"] = fmt (44100, 16, 16);
            store.supported = { { 44100, 16, 16, false }, { 48000, 16, 16, false } };
            FormatPolicy policy (store);
            const auto out = policy.judgeAndApply ("d", 48000, 24, true);
            expect (out.result == FormatResult::unsupported);
            expectEquals (store.writeCalls.load(), 0);
            expect (out.supported.contains ("44100 Hz / 16-bit"));
            expect (out.supported.contains ("48000 Hz / 16-bit"));
        }

        beginTest ("Unknown when support cannot be determined");
        {
            MockFormatStore store;
            store.current["d"] = fmt (44100, 16, 16);
            store.supportResult = result::noInterface;
            FormatPolicy policy (store);
            const auto out = policy.judgeAndApply ("d", 48000, 24, true);
            expect (out.result == FormatResult::unknown);
            expect (out.code == result::noInterface);
            expectEquals (store.writeCalls.load(), 0);
        }

        beginTest ("Unknown when the current format cannot be read");
        {
            MockFormatStore store;
            store.readResult = result::fail;
            FormatPolicy policy (store);
            expect (policy.judgeAndApply ("d", 48000, 24, true).result == FormatResult::unknown);
        }

        beginTest ("Report only when not enforcing");
        {
            MockFormatStore store;
            store.current["d"] = fmt (44100, 16, 16);
            store.supported = { { 48000, 24, 24, false } };
            FormatPolicy policy (store);
            expect (policy.judgeAndApply ("d", 48000, 24, false).result == FormatResult::skipped);
            expectEquals (store.writeCalls.load(), 0);
        }

        beginTest ("Write and verify failures");
        {
            MockFormatStore store;
            store.current["d"] = fmt (44100, 16, 16);
            store.supported = { { 48000, 24, 24, false } };
            store.writeResult = result::accessDenied;
            FormatPolicy policy (store);
            expect (policy.judgeAndApply ("d", 48000, 24, true).result == FormatResult::writeFailed);

            store.writeResult = result::ok;
            store.writesStick = false;
            expect (policy.judgeAndApply ("d", 48000, 24, true).result == FormatResult::verifyFailed);
        }

        beginTest ("Candidate layouts per bit depth");
        {
            expectEquals (static_cast<int> (FormatPolicy::candidates (48000, 16, 2, 3).size()), 1);
            const auto c24 = FormatPolicy::candidates (48000, 24, 2, 3);
            expectEquals (static_cast<int> (c24.size()), 2);
            expectEquals (static_cast<int> (c24[0].containerBits), 24);
            expectEquals (static_cast<int> (c24[1].containerBits), 32);
            const auto c32 = FormatPolicy::candidates (48000, 32, 2, 3);
            expectEquals (static_cast<int> (c32.size()), 2);
            expect (! c32[0].isFloat);
            expect (c32[1].isFloat);
            expect (FormatPolicy::candidates (48000, 20, 2, 3).empty());
        }
    }
};

static FormatPolicyTests formatPolicyTests;
} // namespace audioslave::test
