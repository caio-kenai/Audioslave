#include "platform/windows/WinCommon.h"
#include "cli/Diagnostics.h"
#include "AudioslaveVersion.h"
#include "audio/juce/JuceAudioDeviceInventory.h"
#include "audio/juce/JuceExclusiveModeProbe.h"
#include "audio/windows/WindowsAudioEndpointEnumerator.h"
#include "audio/windows/WindowsAudioFormatPolicy.h"
#include "audio/windows/WindowsExclusiveModePolicy.h"
#include "cli/Commands.h"
#include "config/Configuration.h"
#include "core/ExclusiveModePolicy.h"
#include "core/FormatPolicy.h"
#include "platform/windows/Elevation.h"
#include "platform/windows/Paths.h"
#include "platform/windows/ScopedComInit.h"
#include "platform/windows/WinError.h"
#include "service/ServiceController.h"

#include <juce_events/juce_events.h>

namespace audioslave::cli
{
namespace
{
std::vector<AudioEndpoint> inventory (bool printTable)
{
    win::WindowsAudioEndpointEnumerator enumerator;
    win::WindowsExclusiveModePolicy exclusiveStore;
    win::WindowsAudioFormatPolicy formatStore;
    ExclusiveModePolicy exclusive (exclusiveStore);
    FormatPolicy format (formatStore);

    std::vector<AudioEndpoint> endpoints;
    const auto rc = enumerator.enumerate (endpoints);
    if (failed (rc) && rc != result::notFound)
    {
        printLine ("Enumeration failed: " + win::hresultText (rc));
        return endpoints;
    }
    if (! printTable)
        return endpoints;

    printLine ("\nEndpoints (" + juce::String (static_cast<int> (endpoints.size())) + ")");
    printLine ("----------------------------------------------------------------");
    for (auto& e : endpoints)
    {
        exclusive.inspect (e);
        printLine ("  " + endpointFlowName (e.flow).paddedRight (' ', 8) + (e.isDefault ? "[default]  " : "           ")
                   + endpointStateName (e.state));
        printLine ("       " + e.name);
        printLine ("       id             = " + e.id);
        if (e.description.isNotEmpty())
            printLine ("       name (editable)= " + e.description);
        if (e.exclusive.known)
            printLine ("       exclusive mode = " + juce::String (e.exclusive.allowed ? "ALLOWED " : "blocked ") + "(priority "
                       + (e.exclusive.priority ? "on" : "off") + ")" + (e.exclusive.allowed ? "  <-- NOT COMPLIANT" : ""));
        else
            printLine ("       exclusive mode = unknown (" + win::hresultText (e.exclusive.readResult) + ")");

        if (! e.isActive())
            continue;
        AudioFormat current;
        if (const auto frc = formatStore.getDeviceFormat (e.id, current); failed (frc))
        {
            printLine ("       default format = unknown (" + win::hresultText (frc) + ")");
            continue;
        }
        printLine ("       default format = " + describeFormat (current) + (current.isFloat ? " float" : "") + ", "
                   + juce::String (current.channels) + " ch");
        const auto caps = format.probe (e.id, current);
        printLine ("       sample rates   = " + caps.describeRates());
        printLine ("       bit depths     = " + caps.describeDepths());
        printLine ("       supported      = " + format.describeSupported (e.id, current));
    }
    return endpoints;
}

void printJuceView()
{
    const auto shared = scanJuceWasapiDevices (juce::WASAPIDeviceMode::shared);
    printLine ("\nAs seen by JUCE applications (" + shared.typeName + ")");
    printLine ("----------------------------------------------------------------");
    if (! shared.available)
    {
        printLine ("  (WASAPI not available)");
        return;
    }
    printLine ("  Outputs: " + (shared.outputs.isEmpty() ? juce::String ("(none)") : shared.outputs.joinIntoString (" | ")));
    printLine ("  Inputs:  " + (shared.inputs.isEmpty() ? juce::String ("(none)") : shared.inputs.joinIntoString (" | ")));
}

void printEnvironment()
{
    printLine ("OS                 : " + juce::SystemStats::getOperatingSystemName()
               + (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : ""));
    printLine ("Elevated           : " + juce::String (win::isElevated() ? "yes" : "no"));
    printLine ("Version            : " AUDIOSLAVE_VERSION_STRING " (" + juce::SystemStats::getJUCEVersion() + ")");
    printLine ("Executable         : " + paths::executableFile().getFullPathName());
    printLine ("Config             : " + paths::configFile().getFullPathName());
    printLine ("Logs               : " + paths::logsDir().getFullPathName());
    const auto service = scm::query();
    printLine ("Service            : " + scm::stateName (service.state)
               + (service.binaryPath.isNotEmpty() ? " (" + service.binaryPath + ")" : juce::String()));
}

void printProbe (const AudioEndpoint& e, const ExclusiveProbeResult& r)
{
    printLine ("  " + probeOutcomeName (r.outcome).paddedRight (' ', 12) + "[" + endpointFlowName (e.flow) + "] " + e.name
               + (r.detail.isNotEmpty() ? "  (" + r.detail + ")" : juce::String()));
}
} // namespace

int runDevices()
{
    const win::ScopedComInit com;
    if (! com.isUsable())
    {
        printLine ("COM initialisation failed: " + win::hresultText (com.result()));
        return 2;
    }
    inventory (true);

    const juce::ScopedJuceInitialiser_GUI juce;
    printJuceView();
    return 0;
}

int runDiagnose (bool probeExclusive)
{
    const win::ScopedComInit com;
    if (! com.isUsable())
    {
        printLine ("COM initialisation failed: " + win::hresultText (com.result()));
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juce;

    printEnvironment();
    printLine ("\n-- Endpoint inventory --");
    auto endpoints = inventory (true);
    printJuceView();

    // Store self-test on the default active render endpoint (else the first active one).
    const AudioEndpoint* target = nullptr;
    for (const auto& e : endpoints)
        if (e.flow == EndpointFlow::render && e.isActive() && (target == nullptr || e.isDefault))
            target = &e;
    for (const auto& e : endpoints)
        if (target == nullptr && e.isActive())
            target = &e;

    if (target != nullptr)
    {
        win::WindowsExclusiveModePolicy store;
        printLine ("\n-- Store test on '" + target->name + "' --");
        bool allow = false, priority = false;
        auto rc = store.read (target->id, allow, priority);
        printLine ("  read               -> " + win::hresultText (rc) + " (allow=" + juce::String (allow ? 1 : 0)
                   + ", priority=" + juce::String (priority ? 1 : 0) + ")");
        rc = store.verifyWritable (target->id);
        printLine ("  write (same value) -> " + win::hresultText (rc)
                   + (failed (rc) && ! win::isElevated() ? "  (administrator rights are required to write)" : ""));

        if (probeExclusive)
        {
            printLine ("\n-- JUCE exclusive-mode probe --");
            printProbe (*target, probeExclusiveMode (target->name, target->flow));
        }
    }

    printLine ("\n-- Config --");
    const auto loaded = loadConfiguration (paths::configFile(), false);
    const auto& cfg = loaded.config;
    printLine ("  file                 = " + paths::configFile().getFullPathName() + (loaded.fileFound ? "" : " (missing: defaults)"));
    printLine ("  monitorPlayback      = " + juce::String (cfg.monitorPlayback ? "true" : "false"));
    printLine ("  monitorCapture       = " + juce::String (cfg.monitorCapture ? "true" : "false"));
    printLine ("  checkIntervalSeconds = " + juce::String (cfg.checkIntervalSeconds));
    printLine ("  enforce              = " + juce::String (cfg.enforce ? "true" : "false"));
    printLine ("  exclusive protection = " + juce::String (cfg.exclusiveModeProtection ? "ENABLED" : "DISABLED"));
    printLine ("  format standardize   = "
               + (cfg.formatStandardization ? "ENABLED (" + describeFormatTarget (cfg) + ")" : juce::String ("DISABLED")));
    for (const auto& w : loaded.warnings)
        printLine ("  warning: " + w);
    return 0;
}

int runValidate()
{
    const win::ScopedComInit com;
    if (! com.isUsable())
    {
        printLine ("COM initialisation failed: " + win::hresultText (com.result()));
        return 2;
    }
    const juce::ScopedJuceInitialiser_GUI juce;
    const auto cfg = loadConfiguration (paths::configFile(), false).config;

    printLine ("Opening every active endpoint through JUCE's WASAPI exclusive mode...");
    int allowed = 0, blocked = 0, other = 0;
    for (const auto& e : inventory (false))
    {
        if (! e.isActive() || (e.flow == EndpointFlow::render && ! cfg.monitorPlayback)
            || (e.flow == EndpointFlow::capture && ! cfg.monitorCapture))
            continue;
        const auto r = probeExclusiveMode (e.name, e.flow);
        printProbe (e, r);
        if (r.outcome == ExclusiveProbeResult::Outcome::allowed)
            ++allowed;
        else if (r.outcome == ExclusiveProbeResult::Outcome::blocked)
            ++blocked;
        else
            ++other;
    }
    printLine ("\n" + juce::String (blocked) + " blocked, " + juce::String (allowed) + " allowed, " + juce::String (other)
               + " not probed.");
    if (allowed > 0 && cfg.exclusiveModeProtection)
    {
        printLine ("Exclusive mode is still possible on " + juce::String (allowed) + " endpoint(s).");
        return 1;
    }
    return 0;
}
} // namespace audioslave::cli
