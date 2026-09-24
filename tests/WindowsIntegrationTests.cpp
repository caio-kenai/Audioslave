// Read-only integration tests against the real Windows audio stack
// (category "Integration", run with --integration). Nothing is written.

#include "platform/windows/WinCommon.h"
#include "audio/juce/JuceAudioDeviceInventory.h"
#include "audio/windows/ComHelpers.h"
#include "audio/windows/WindowsAudioEndpointEnumerator.h"
#include "audio/windows/WindowsAudioFormatPolicy.h"
#include "audio/windows/WindowsExclusiveModePolicy.h"
#include "audio/windows/WindowsFormatSupport.h"
#include "core/FormatPolicy.h"
#include "platform/windows/Paths.h"
#include "platform/windows/ScopedComInit.h"
#include "platform/windows/WinError.h"
#include "service/ServiceController.h"
#include "tests/Mocks.h"

namespace audioslave::test
{
class WindowsIntegrationTests final : public juce::UnitTest
{
public:
    WindowsIntegrationTests() : juce::UnitTest ("Windows audio (read-only)", "Integration") {}

    void runTest() override
    {
        const win::ScopedComInit com;
        std::vector<AudioEndpoint> endpoints;

        beginTest ("Endpoint enumeration (MMDevice)");
        {
            expect (com.isUsable(), win::hresultText (com.result()));
            win::WindowsAudioEndpointEnumerator enumerator;
            const auto rc = enumerator.enumerate (endpoints);
            expect (succeeded (rc) || rc == result::notFound, win::hresultText (rc));
            for (const auto& e : endpoints)
            {
                expect (e.id.isNotEmpty());
                expect (e.flow != EndpointFlow::unknown);
                logMessage ("    " + endpointFlowName (e.flow) + " | " + endpointStateName (e.state) + " | " + e.name);
            }
        }

        beginTest ("Exclusive-mode state is readable on every endpoint");
        {
            win::WindowsExclusiveModePolicy store;
            for (const auto& e : endpoints)
            {
                bool allow = false, priority = false;
                const auto rc = store.read (e.id, allow, priority);
                expect (succeeded (rc), e.name + ": " + win::hresultText (rc));
                logMessage ("    " + e.name + ": exclusive " + (allow ? "ALLOWED" : "blocked"));
            }
        }

        beginTest ("Default format and driver-reported support (IKsFormatSupport)");
        {
            win::WindowsAudioFormatPolicy store;
            FormatPolicy policy (store);
            for (const auto& e : endpoints)
            {
                if (! e.isActive())
                    continue;
                AudioFormat current;
                const auto rc = store.getDeviceFormat (e.id, current);
                expect (succeeded (rc), e.name + ": " + win::hresultText (rc));
                if (failed (rc))
                    continue;
                expect (current.sampleRate >= 8000);
                bool supported = false;
                const auto src = store.isFormatSupported (e.id, current, supported);
                // Some virtual drivers expose no KS streaming pin (E_NOINTERFACE).
                expect (succeeded (src) || src == result::noInterface, e.name + ": " + win::hresultText (src));
                logMessage ("    " + e.name + ": " + describeFormat (current) + " | supported: "
                            + policy.describeSupported (e.id, current));
            }
        }

        beginTest ("Capability probe agrees with the per-format answers and the current format");
        {
            win::WindowsAudioFormatPolicy store;
            FormatPolicy policy (store);
            for (const auto& e : endpoints)
            {
                if (! e.isActive())
                    continue;
                AudioFormat current;
                if (failed (store.getDeviceFormat (e.id, current)))
                    continue;
                const auto started = juce::Time::getMillisecondCounterHiRes();
                const auto caps = policy.probe (e.id, current);
                const auto ms = juce::Time::getMillisecondCounterHiRes() - started;
                if (! caps.known)
                {
                    expect (caps.code == result::noInterface, e.name + ": " + win::hresultText (caps.code));
                    logMessage ("    " + e.name + ": capabilities unknown (" + win::hresultText (caps.code) + ")");
                    continue;
                }
                // The batch probe must match the one-by-one answer.
                for (auto rate : { 44100u, 48000u })
                    for (auto bits : { std::uint16_t (16), std::uint16_t (24) })
                    {
                        bool single = false;
                        for (const auto& f : FormatPolicy::candidates (rate, bits, current.channels, current.channelMask))
                        {
                            bool ok = false;
                            store.isFormatSupported (e.id, f, ok);
                            single = single || ok;
                        }
                        expect (caps.supports (rate, bits) == single, e.name + " " + juce::String (rate) + "/" + juce::String (bits));
                    }
                logMessage ("    " + e.name + ": " + caps.describeRates() + " | " + caps.describeDepths() + " ("
                            + juce::String (ms, 0) + " ms)");
            }
        }

        beginTest ("JUCE WASAPI sees the active endpoints");
        {
            const auto juceView = scanJuceWasapiDevices (juce::WASAPIDeviceMode::shared);
            expect (juceView.available);
            int active = 0;
            for (const auto& e : endpoints)
                active += e.isActive() ? 1 : 0;
            expectEquals (juceView.outputs.size() + juceView.inputs.size(), active);
        }

        beginTest ("SCM queries do not fail");
        {
            const auto status = scm::query();
            expect (status.error.isEmpty(), status.error);
            logMessage ("    Audioslave service: " + scm::stateName (status.state));
        }
    }
};

static WindowsIntegrationTests windowsIntegrationTests;
} // namespace audioslave::test
