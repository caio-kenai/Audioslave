#include "core/WatchdogEngine.h"
#include "logging/Logger.h"

#include <algorithm>

namespace audioslave
{
juce::String engineStateName (EngineState s)
{
    switch (s)
    {
        case EngineState::running:  return "RUNNING";
        case EngineState::paused:   return "PAUSED";
        case EngineState::stopping: return "STOPPING";
        case EngineState::stopped:  return "STOPPED";
    }
    return "UNKNOWN";
}

namespace
{
constexpr std::pair<DeviceAction, const char*> actionNames[] = {
    { DeviceAction::none, "none" },           { DeviceAction::compliant, "compliant" },
    { DeviceAction::apply, "apply" },         { DeviceAction::applied, "applied" },
    { DeviceAction::ignore, "ignore" },       { DeviceAction::disable, "disable" },
    { DeviceAction::disabled, "disabled" },   { DeviceAction::keepDisabled, "keep-disabled" },
    { DeviceAction::reenable, "reenable" },   { DeviceAction::reenabled, "reenabled" },
    { DeviceAction::pending, "pending" },     { DeviceAction::leftEnabled, "left-enabled" },
    { DeviceAction::unknown, "unknown" },     { DeviceAction::failed, "failed" },
};
} // namespace

juce::String deviceActionName (DeviceAction a)
{
    for (const auto& [action, name] : actionNames)
        if (action == a)
            return name;
    return "none";
}

DeviceAction deviceActionFromName (const juce::String& text)
{
    for (const auto& [action, name] : actionNames)
        if (text == name)
            return action;
    return DeviceAction::none;
}

namespace
{
constexpr juce::int64 dayMs = 24ll * 3600ll * 1000ll;

juce::String resultText (Compatibility c)
{
    switch (c)
    {
        case Compatibility::compatible:       return "Compatible";
        case Compatibility::rateUnsupported:  return "Incompatible (sample rate)";
        case Compatibility::depthUnsupported: return "Incompatible (bit depth)";
        case Compatibility::unknown:          break;
    }
    return "Unknown";
}

// One line per device and decision, e.g.
// [AudioFormat] Device: USB Headset | ID: {...} | Type: capture | Requested: 48000 Hz / 24-bit |
//   Supported: 44100, 48000 Hz / 16-bit | Result: Incompatible (bit depth) | Reason: ... | Action: Ignored
juce::String formatLine (const DeviceReport& d, const juce::String& requested, const juce::String& action)
{
    return "[AudioFormat] Device: " + d.name + " | ID: " + d.id + " | Type: " + endpointFlowName (d.flow)
           + " | Current: " + (d.currentFormat.isNotEmpty() ? d.currentFormat : juce::String ("n/a"))
           + " | Requested: " + requested + " | Supported: " + d.capabilities.describeRates() + " / "
           + d.capabilities.describeDepths() + " | Result: " + resultText (d.compatibility) + " | Reason: " + d.reason
           + " | Action: " + action;
}

juce::String hex (ResultCode code)
{
    return "0x" + juce::String::toHexString (static_cast<juce::uint32> (code)).toUpperCase().paddedLeft ('0', 8);
}

juce::String exclusiveText (const AudioEndpoint& e)
{
    if (! e.exclusive.known)
        return "unknown";
    return e.exclusive.allowed ? "allowed" : "blocked";
}
} // namespace

WatchdogEngine::WatchdogEngine (IAudioEndpointEnumerator& enumerator, IExclusiveModeStore& exclusiveStore,
                                IAudioFormatStore& formatStore, Configuration config, Options options)
    : juce::Thread ("Audioslave watchdog"),
      enumerator_ (enumerator),
      exclusivePolicy_ (exclusiveStore),
      formatPolicy_ (formatStore),
      options_ (std::move (options)),
      memory_ (options_.clock),
      config_ (std::move (config))
{
    status_.state = EngineState::running;
}

WatchdogEngine::~WatchdogEngine()
{
    stop();
}

bool WatchdogEngine::start()
{
    if (isThreadRunning())
        return true;
    setState (EngineState::running);
    if (! startThread())
    {
        setState (EngineState::stopped);
        return false;
    }
    return true;
}

void WatchdogEngine::stop()
{
    if (! isThreadRunning())
    {
        if (state_.load() == EngineState::stopping)
            setState (EngineState::stopped);
        return;
    }
    setState (EngineState::stopping);
    signalThreadShouldExit();
    notify();
    // A pass is bounded (a handful of COM calls per endpoint), so the worker
    // always comes back: wait for it instead of ever killing it.
    waitForThreadToExit (-1);
    setState (EngineState::stopped);
}

void WatchdogEngine::setState (EngineState newState)
{
    if (state_.exchange (newState) != newState)
        listeners_.call ([newState] (Listener& l) { l.engineStateChanged (newState); });
}

void WatchdogEngine::pause()
{
    bool changed = false;
    pauseRequested_ = true;
    {
        const juce::ScopedLock sl (modifyLock_); // waits for the endpoint being processed
        auto expected = EngineState::running;
        changed = state_.compare_exchange_strong (expected, EngineState::paused);
    }
    pauseRequested_ = false;
    if (changed)
    {
        Logger::instance().info ("Monitoring PAUSED: no audio device will be modified until resumed.");
        listeners_.call ([] (Listener& l) { l.engineStateChanged (EngineState::paused); });
    }
}

void WatchdogEngine::resume()
{
    auto expected = EngineState::paused;
    if (! state_.compare_exchange_strong (expected, EngineState::running))
        return;
    memory_.clear();
    Logger::instance().info ("Monitoring RESUMED: running a full device scan.");
    listeners_.call ([] (Listener& l) { l.engineStateChanged (EngineState::running); });
    requestFullScan();
}

void WatchdogEngine::requestRescan()
{
    rescanPending_ = true;
    notify();
}

void WatchdogEngine::requestFullScan()
{
    fullScanPending_ = true;
    requestRescan();
}

Configuration WatchdogEngine::getConfig() const
{
    const juce::ScopedLock sl (configLock_);
    return config_;
}

void WatchdogEngine::setConfig (const Configuration& config)
{
    bool policyChanged = false;
    {
        const juce::ScopedLock sl (configLock_);
        policyChanged = config.formatStandardization != config_.formatStandardization || config.sampleRate != config_.sampleRate
                        || config.bitDepth != config_.bitDepth
                        || config.disableIncompatibleDevices != config_.disableIncompatibleDevices
                        || config.disableConfirmedFor != config_.disableConfirmedFor;
        config_ = config;
    }
    memory_.clear();
    // A new decision by the user: devices left enabled get a new chance.
    if (policyChanged)
        deviceState().resetHistory();
}

void WatchdogEngine::forgetCapabilities()
{
    const juce::ScopedLock sl (capabilitiesLock_);
    capabilities_.clear();
}

FormatCapabilities WatchdogEngine::capabilitiesFor (const juce::String& endpointId, const AudioFormat& current)
{
    {
        const juce::ScopedLock sl (capabilitiesLock_);
        if (const auto it = capabilities_.find (endpointId); it != capabilities_.end())
            return it->second;
    }
    // Asked once per device (1-3 ms); again after a device change or "Verificar agora".
    auto caps = formatPolicy_.probe (endpointId, current);
    if (caps.known)
    {
        const juce::ScopedLock sl (capabilitiesLock_);
        capabilities_[endpointId] = caps;
    }
    return caps;
}

void WatchdogEngine::addEvent (DeviceAction action, const DeviceReport& device, bool interactive)
{
    DeviceEvent e;
    e.action = action;
    e.id = device.id;
    e.name = device.name;
    e.reason = device.reason;
    e.interactive = interactive;
    e.timeMs = now();
    const juce::ScopedLock sl (statusLock_);
    e.sequence = nextEventSequence_++;
    events_.push_back (e);
    while (events_.size() > maxEvents)
        events_.pop_front();
}

EngineStatus WatchdogEngine::getStatus() const
{
    const juce::ScopedLock sl (statusLock_);
    auto copy = status_;
    copy.state = state_.load();
    copy.events.assign (events_.begin(), events_.end());
    return copy;
}

void WatchdogEngine::notice (const juce::String& key, const juce::String& message)
{
    if (memory_.notice (key, message))
        Logger::instance().warn (message);
    else
        Logger::instance().debug (message);
}

void WatchdogEngine::onDeviceChange (const DeviceChange& change)
{
    auto& log = Logger::instance();

    juce::String label = change.endpointId;
    {
        const juce::ScopedLock sl (statusLock_);
        for (const auto& e : status_.endpoints)
            if (e.id == change.endpointId)
            {
                label = e.name + " " + change.endpointId;
                break;
            }
    }

    switch (change.kind)
    {
        case DeviceChange::Kind::added:
            log.info ("Device added: " + label);
            break;
        case DeviceChange::Kind::removed:
            log.info ("Device removed: " + label);
            break;
        case DeviceChange::Kind::stateChanged:
            log.info ("Device state changed to " + endpointStateName (change.newState) + ": " + label);
            break;
        case DeviceChange::Kind::defaultChanged:
            log.debug ("Default device changed: " + label);
            break;
        case DeviceChange::Kind::propertyChanged:
            // Frequent (our own writes trigger it too): just rescan.
            requestRescan();
            return;
    }
    // A (re)created or re-plugged endpoint is a new situation: check and
    // report it afresh, and ask its driver for its formats again.
    memory_.forgetEndpoint (change.endpointId);
    {
        const juce::ScopedLock sl (capabilitiesLock_);
        capabilities_.erase (change.endpointId);
    }
    requestRescan();
}

void WatchdogEngine::run()
{
    const auto threadScope = options_.workerThreadScope ? options_.workerThreadScope() : nullptr;

    auto kind = ScanKind::full;
    while (! threadShouldExit())
    {
        bool enumerationFailed = false;
        if (canModify())
        {
            try
            {
                enumerationFailed = scanOnce (kind).enumerationFailed;
            }
            catch (const std::exception& e)
            {
                Logger::instance().error ("Scan aborted by an exception: " + juce::String (e.what()));
            }
            catch (...)
            {
                Logger::instance().error ("Scan aborted by an unknown exception.");
            }
        }

        int waitMs = static_cast<int> (getConfig().checkIntervalSeconds) * 1000;
        if (enumerationFailed)
            waitMs = juce::jmin (waitMs, options_.enumerationRetryMs);

        const bool woken = wait (waitMs);
        if (threadShouldExit())
            break;

        if (woken && rescanPending_.exchange (false))
        {
            // Let a burst of notifications settle into a single pass.
            for (int i = 0; i < 10; ++i)
            {
                if (! wait (options_.debounceMs))
                    break;
                if (threadShouldExit())
                    return;
            }
            rescanPending_ = false;
            kind = fullScanPending_.exchange (false) ? ScanKind::full : ScanKind::triggered;
        }
        else
        {
            kind = ScanKind::full; // periodic safety net
            fullScanPending_ = false;
        }
    }
}

ScanReport WatchdogEngine::scanOnce (ScanKind kind, bool interactive)
{
    ScanReport report;
    report.kind = kind;
    auto& log = Logger::instance();

    if (! canModify())
    {
        report.paused = true;
        return report;
    }

    const juce::ScopedLock scanGuard (scanLock_);
    const auto cfg = getConfig();
    std::vector<AudioEndpoint> endpoints;
    const auto rc = enumerator_.enumerate (endpoints);
    std::vector<EndpointStatus> snapshot;
    std::vector<DeviceReport> pending;

    if (rc == result::notFound)
    {
        log.debug ("No audio endpoints present.");
    }
    else if (failed (rc))
    {
        log.error ("Endpoint enumeration failed: " + hex (rc));
        report.enumerationFailed = true;
    }

    for (auto& endpoint : endpoints)
    {
        if ((endpoint.flow == EndpointFlow::render && ! cfg.monitorPlayback)
            || (endpoint.flow == EndpointFlow::capture && ! cfg.monitorCapture))
            continue;

        // A device disabled by the user (or anything but Audioslave) is none
        // of Audioslave's business.
        const auto record = deviceState().get (endpoint.id);
        const bool disabledByUs = record.has_value() && record->disabled;
        // The enumeration is a snapshot: before believing that a device
        // Audioslave disabled is enabled again, ask Windows now.
        if (disabledByUs && endpoint.state != EndpointState::disabled && options_.admin != nullptr)
            if (EndpointState live = endpoint.state; succeeded (options_.admin->getState (endpoint.id, live)))
                endpoint.state = live;
        if (endpoint.state == EndpointState::disabled && ! disabledByUs)
            continue;
        if (endpoint.state == EndpointState::notPresent)
            continue;

        const juce::ScopedLock sl (modifyLock_);
        // Pausing mid-pass stops further modifications immediately.
        if (! canModify())
        {
            report.paused = true;
            break;
        }

        ++report.endpointsScanned;
        const bool backoff = kind == ScanKind::triggered && memory_.inBackoff (endpoint.id);
        const bool enforce = cfg.enforce && ! backoff;
        const auto& name = endpoint.name;

        EndpointStatus entry;
        entry.id = endpoint.id;
        entry.name = endpoint.name;
        entry.flow = endpoint.flow;
        entry.state = endpoint.state;
        entry.isDefault = endpoint.isDefault;
        entry.description = endpoint.description;

        // --- Name chosen by the user ------------------------------------------
        if (const auto wanted = customDeviceName (cfg, endpoint.id); wanted.isNotEmpty())
        {
            entry.customName = true;
            restoreName (endpoint, wanted, enforce, report, entry);
        }

        DeviceReport device;
        device.id = endpoint.id;
        device.name = endpoint.name;
        device.flow = endpoint.flow;
        device.state = endpoint.state;
        device.isDefault = endpoint.isDefault;

        // --- Disabled earlier by Audioslave ------------------------------------
        if (endpoint.state == EndpointState::disabled)
        {
            handleDisabledByAudioslave (endpoint, *record, cfg, enforce, interactive, report, device);
            entry.state = device.state;
            entry.disabledByAudioslave = device.disabledByAudioslave;
            entry.compatibility = device.compatibility;
            entry.capabilities = device.capabilities.serialise();
            entry.action = device.action;
            entry.reason = device.reason;
            report.devices.push_back (device);
            snapshot.push_back (std::move (entry));
            continue;
        }

        // --- Exclusive Mode Protection ---------------------------------------
        if (cfg.exclusiveModeProtection)
        {
            switch (exclusivePolicy_.judgeAndFix (endpoint, enforce))
            {
                case ExclusiveResult::fixed:
                    ++report.exclusiveFixed;
                    memory_.clearFailure (endpoint.id);
                    log.warn ("[" + name + "] exclusive mode was ENABLED -> forced off (verified)");
                    break;
                case ExclusiveResult::alreadyOff:
                    ++report.exclusiveAlreadyOff;
                    break;
                case ExclusiveResult::unknown:
                    ++report.exclusiveFailed;
                    notice ("excl|" + endpoint.id,
                            "[" + name + "] could not read the exclusive-mode state (" + hex (endpoint.exclusive.readResult) + ")");
                    break;
                case ExclusiveResult::writeFailed:
                    ++report.exclusiveFailed;
                    memory_.recordFailure (endpoint.id);
                    log.error ("[" + name + "] FAILED to disable exclusive mode");
                    break;
                case ExclusiveResult::verifyFailed:
                    ++report.exclusiveFailed;
                    memory_.recordFailure (endpoint.id);
                    log.error ("[" + name + "] exclusive-mode write did not stick (verify failed)");
                    break;
                case ExclusiveResult::skipped:
                    ++report.exclusiveSkipped;
                    log.debug ("[" + name + "] exclusive mode enabled but not changed ("
                               + (backoff ? "recent failure, retry on the next full scan" : "enforcement off, report only") + ")");
                    break;
            }
        }
        else
        {
            exclusivePolicy_.inspect (endpoint); // report only
        }
        entry.exclusive = exclusiveText (endpoint);

        // --- Audio Format Standardization (active endpoints only) -------------
        if (endpoint.isActive())
        {
            if (cfg.formatStandardization)
            {
                const auto key = "fmt|" + endpoint.id;
                const auto target = describeFormatTarget (cfg);
                const auto out = formatPolicy_.judgeAndApply (endpoint.id, cfg.sampleRate, cfg.bitDepth, enforce);
                device.currentFormat = out.result == FormatResult::unknown ? juce::String() : describeFormat (out.before);
                switch (out.result)
                {
                    case FormatResult::compliant:
                        ++report.formatCompliant;
                        entry.format = describeFormat (out.before);
                        device.compatibility = Compatibility::compatible;
                        device.action = DeviceAction::compliant;
                        break;
                    case FormatResult::applied:
                        ++report.formatApplied;
                        memory_.clearFailure (endpoint.id);
                        entry.format = describeFormat (out.applied);
                        device.compatibility = Compatibility::compatible;
                        device.action = DeviceAction::applied;
                        log.info ("[" + name + "] default format changed: " + describeFormat (out.before) + " -> " + target);
                        break;
                    case FormatResult::unsupported:
                        ++report.formatUnsupported;
                        entry.format = describeFormat (out.before);
                        handleIncompatible (endpoint, cfg, enforce, interactive, report, device);
                        if (device.action == DeviceAction::pending)
                            pending.push_back (device);
                        break;
                    case FormatResult::unknown:
                        ++report.formatFailed;
                        device.action = DeviceAction::unknown;
                        device.reason = "the current or supported formats could not be determined (" + hex (out.code) + ")";
                        notice (key, "[" + name + "] format standardization skipped: " + device.reason + ".");
                        break;
                    case FormatResult::writeFailed:
                        ++report.formatFailed;
                        memory_.recordFailure (endpoint.id);
                        entry.format = describeFormat (out.before);
                        device.compatibility = Compatibility::compatible;
                        device.action = DeviceAction::failed;
                        device.reason = "the driver rejected " + target + " (" + hex (out.code) + ")";
                        log.error ("[" + name + "] FAILED to set the default format " + target + " (" + hex (out.code) + ")");
                        break;
                    case FormatResult::verifyFailed:
                        ++report.formatFailed;
                        memory_.recordFailure (endpoint.id);
                        device.compatibility = Compatibility::compatible;
                        device.action = DeviceAction::failed;
                        device.reason = target + " was accepted but did not stick";
                        log.error ("[" + name + "] default format " + target + " did not stick (verify failed)");
                        break;
                    case FormatResult::skipped:
                        ++report.formatSkipped;
                        entry.format = describeFormat (out.before);
                        device.compatibility = Compatibility::compatible;
                        device.action = DeviceAction::apply;
                        device.reason = backoff ? "recent failure" : "enforcement off (report only)";
                        log.debug ("[" + name + "] format is " + describeFormat (out.before) + ", target " + target
                                   + " not applied (" + device.reason + ")");
                        break;
                }
                if (out.result == FormatResult::compliant || out.result == FormatResult::applied)
                {
                    memory_.clearNotice (key);
                    // Compatible again: nothing left to remember about it.
                    if (record.has_value())
                        deviceState().remove (endpoint.id);
                }
                entry.compatibility = device.compatibility;
                entry.capabilities = device.capabilities.serialise();
                entry.action = device.action;
                entry.reason = device.reason;
                report.devices.push_back (device);
            }
            else
            {
                // Report only, for the status window.
                AudioFormat current;
                if (succeeded (formatPolicy_.currentFormat (endpoint.id, current)))
                    entry.format = describeFormat (current);
            }
        }
        snapshot.push_back (std::move (entry));
    }

    const auto summary = juce::String ("Scan (") + (kind == ScanKind::full ? "full" : "triggered") + "): "
                         + juce::String (report.endpointsScanned) + " endpoint(s) | exclusive: "
                         + juce::String (report.exclusiveFixed) + " fixed, " + juce::String (report.exclusiveAlreadyOff)
                         + " ok | format: " + juce::String (report.formatApplied) + " applied, "
                         + juce::String (report.formatCompliant) + " ok, " + juce::String (report.formatUnsupported)
                         + " unsupported | devices: " + juce::String (report.devicesDisabled) + " disabled, "
                         + juce::String (report.devicesReenabled) + " re-enabled | errors: " + juce::String (report.errors());
    if (report.changedSomething())
        log.info (summary);
    else
        log.debug (summary);

    {
        const juce::ScopedLock sl (statusLock_);
        status_.hasScanned = true;
        status_.lastScan = report;
        status_.lastScan.devices.clear(); // per-device detail is not published with the status
        status_.lastScanTime = juce::Time::getCurrentTime();
        ++status_.totalScans;
        status_.totalExclusiveFixes += report.exclusiveFixed;
        status_.totalFormatChanges += report.formatApplied;
        if (! report.paused)
        {
            status_.endpoints = std::move (snapshot);
            status_.pendingDisable = std::move (pending);
        }
    }
    listeners_.call ([&report] (Listener& l) { l.scanCompleted (report); });
    return report;
}

void WatchdogEngine::restoreName (const AudioEndpoint& endpoint, const juce::String& wanted, bool enforce,
                                  ScanReport& report, EndpointStatus& entry)
{
    // Unreadable description: nothing to compare with, never write blindly.
    if (options_.admin == nullptr || endpoint.description.isEmpty() || endpoint.description == wanted)
        return;
    auto& log = Logger::instance();
    const auto key = "name|" + endpoint.id;
    if (! enforce)
    {
        log.debug ("[" + endpoint.name + "] name is '" + endpoint.description + "', kept name '" + wanted
                   + "' not restored (report only / recent failure)");
        return;
    }
    if (const auto rc = options_.admin->setDescription (endpoint.id, wanted); failed (rc))
    {
        memory_.recordFailure (endpoint.id);
        notice (key, "[" + endpoint.name + "] could not restore the name '" + wanted + "' (" + hex (rc) + ")");
        return;
    }
    memory_.clearNotice (key);
    ++report.namesRestored;
    entry.description = wanted;
    log.info ("[" + endpoint.name + "] name restored to '" + wanted + "' (it was '" + endpoint.description
              + "': reset by Windows or the driver). ID: " + endpoint.id);
}

void WatchdogEngine::handleIncompatible (const AudioEndpoint& endpoint, const Configuration& cfg, bool enforce,
                                         bool interactive, ScanReport& report, DeviceReport& device)
{
    auto& log = Logger::instance();
    const auto key = "fmt|" + endpoint.id;
    const auto target = describeFormatTarget (cfg);

    AudioFormat current;
    formatPolicy_.currentFormat (endpoint.id, current);
    device.capabilities = capabilitiesFor (endpoint.id, current);
    const auto verdict = judgeCompatibility (device.capabilities, cfg.sampleRate, cfg.bitDepth);
    device.compatibility = verdict.compatibility;
    device.reason = verdict.reason (cfg.sampleRate, cfg.bitDepth);

    if (! cfg.disableIncompatibleDevices || verdict.compatibility == Compatibility::unknown)
    {
        // Never disable a device whose formats are not known.
        device.action = verdict.compatibility == Compatibility::unknown ? DeviceAction::unknown : DeviceAction::ignore;
        notice (key, formatLine (device, target, "Ignored (device left as it is)"));
        return;
    }
    if (! disablePolicyConfirmed (cfg))
    {
        device.action = DeviceAction::pending;
        notice (key, formatLine (device, target, "None yet (would be disabled; waiting for the user's confirmation)"));
        return;
    }
    if (options_.admin == nullptr || ! enforce)
    {
        device.action = DeviceAction::ignore;
        notice (key, formatLine (device, target, options_.admin == nullptr ? "Ignored (devices cannot be disabled from here)"
                                                                           : "Ignored (report only / recent failure)"));
        return;
    }

    // Loop protection: how often has it come back after being disabled?
    const auto t = now();
    auto record = deviceState().get (endpoint.id).value_or (DisabledDeviceRecord {});
    record.history.erase (std::remove_if (record.history.begin(), record.history.end(), [t] (juce::int64 h) { return t - h > dayMs; }),
                          record.history.end());
    if (record.id.isNotEmpty() && record.disabled)
    {
        // It was disabled and is enabled again (by the user, Windows or its driver).
        record.disabled = false;
        log.warn ("[" + endpoint.name + "] was disabled by Audioslave and is enabled again (by the user, Windows or its driver).");
    }
    if (record.leftEnabled)
    {
        device.action = DeviceAction::leftEnabled;
        device.reason += "; it kept coming back enabled, so it is left enabled until the settings change";
        deviceState().put (record);
        notice (key, formatLine (device, target, "Left enabled (came back too often)"));
        return;
    }
    if (static_cast<int> (record.history.size()) >= maxDisablesPerDay)
    {
        record.leftEnabled = true;
        deviceState().put (record);
        device.action = DeviceAction::leftEnabled;
        device.reason += "; disabled " + juce::String (maxDisablesPerDay)
                         + " times in 24 h and enabled again each time, so it is left enabled until the settings change";
        addEvent (DeviceAction::leftEnabled, device, interactive);
        log.warn (formatLine (device, target, "Left enabled (no disable loop)"));
        return;
    }
    if (! record.history.empty() && t - record.history.back() < reapplyCooldownMs)
    {
        deviceState().put (record);
        device.action = DeviceAction::ignore;
        notice (key, formatLine (device, target, "Waiting (enabled again moments after being disabled; retried after "
                                                  + juce::String (reapplyCooldownMs / 60000) + " min)"));
        return;
    }

    EndpointState after = EndpointState::active;
    auto rc = options_.admin->setEnabled (endpoint.id, false);
    if (succeeded (rc))
        rc = options_.admin->getState (endpoint.id, after);
    if (failed (rc) || after != EndpointState::disabled)
    {
        memory_.recordFailure (endpoint.id);
        ++report.formatFailed;
        device.action = DeviceAction::failed;
        device.reason = "could not be disabled (" + (failed (rc) ? hex (rc) : juce::String ("still ") + endpointStateName (after)) + ")";
        addEvent (DeviceAction::failed, device, interactive);
        log.error (formatLine (device, target, "Disable FAILED"));
        return;
    }

    record.id = endpoint.id;
    record.name = endpoint.name;
    record.flow = endpoint.flow;
    record.disabledAtMs = t;
    record.requested = target;
    record.reason = device.reason;
    record.compatibility = device.compatibility;
    record.capabilities = device.capabilities;
    record.history.push_back (t);
    record.disabled = true;
    deviceState().put (record);

    memory_.clearFailure (endpoint.id);
    memory_.clearNotice (key);
    ++report.devicesDisabled;
    device.action = DeviceAction::disabled;
    device.state = EndpointState::disabled;
    device.disabledByAudioslave = true;
    addEvent (DeviceAction::disabled, device, interactive);
    log.warn (formatLine (device, target, "Disabled (verified)") + " | Time: " + juce::Time (t).toString (true, true, true, true));
}

void WatchdogEngine::handleDisabledByAudioslave (const AudioEndpoint& endpoint, const DisabledDeviceRecord& record,
                                                 const Configuration& cfg, bool enforce, bool interactive,
                                                 ScanReport& report, DeviceReport& device)
{
    auto& log = Logger::instance();
    const auto target = describeFormatTarget (cfg);
    device.disabledByAudioslave = true;
    device.capabilities = record.capabilities; // asked before it was disabled
    const auto verdict = judgeCompatibility (record.capabilities, cfg.sampleRate, cfg.bitDepth);
    device.compatibility = verdict.compatibility;
    device.reason = verdict.reason (cfg.sampleRate, cfg.bitDepth);

    const bool stillIncompatible = verdict.compatibility == Compatibility::rateUnsupported
                                   || verdict.compatibility == Compatibility::depthUnsupported;
    if (disablePolicyConfirmed (cfg) && stillIncompatible)
    {
        device.action = DeviceAction::keepDisabled;
        return;
    }

    const juce::String why = ! cfg.formatStandardization          ? "format standardization was turned off"
                             : ! cfg.disableIncompatibleDevices   ? "the disable policy was turned off"
                             : ! stillIncompatible                ? "it supports " + target + " now"
                                                                  : "the policy is not confirmed for " + target;
    if (options_.admin == nullptr || ! enforce)
    {
        device.action = DeviceAction::reenable;
        device.reason = why;
        return;
    }

    EndpointState after = EndpointState::disabled;
    auto rc = options_.admin->setEnabled (endpoint.id, true);
    if (succeeded (rc))
        rc = options_.admin->getState (endpoint.id, after);
    if (failed (rc) || after == EndpointState::disabled)
    {
        memory_.recordFailure (endpoint.id);
        ++report.formatFailed;
        device.action = DeviceAction::failed;
        device.reason = "could not be enabled again (" + (failed (rc) ? hex (rc) : juce::String ("still disabled")) + ")";
        log.error ("[" + endpoint.name + "] " + device.reason + ". ID: " + endpoint.id);
        return;
    }
    deviceState().remove (endpoint.id);
    ++report.devicesReenabled;
    device.action = DeviceAction::reenabled;
    device.state = after;
    device.disabledByAudioslave = false;
    device.reason = why;
    addEvent (DeviceAction::reenabled, device, interactive);
    log.info ("[" + endpoint.name + "] enabled again by Audioslave: " + why + ". ID: " + endpoint.id);
}

std::vector<DeviceReport> WatchdogEngine::analyze (const Configuration& candidate)
{
    std::vector<DeviceReport> out;
    std::vector<AudioEndpoint> endpoints;
    if (failed (enumerator_.enumerate (endpoints)))
        return out;

    for (const auto& endpoint : endpoints)
    {
        if ((endpoint.flow == EndpointFlow::render && ! candidate.monitorPlayback)
            || (endpoint.flow == EndpointFlow::capture && ! candidate.monitorCapture))
            continue;
        const auto record = deviceState().get (endpoint.id);
        const bool ours = record.has_value() && record->disabled && endpoint.state == EndpointState::disabled;
        if (! endpoint.isActive() && ! ours)
            continue;

        DeviceReport d;
        d.id = endpoint.id;
        d.name = endpoint.name;
        d.flow = endpoint.flow;
        d.state = endpoint.state;
        d.isDefault = endpoint.isDefault;
        d.disabledByAudioslave = ours;

        AudioFormat current;
        bool currentKnown = false;
        if (ours)
        {
            d.capabilities = record->capabilities;
        }
        else
        {
            currentKnown = succeeded (formatPolicy_.currentFormat (endpoint.id, current));
            if (currentKnown)
                d.currentFormat = describeFormat (current);
            d.capabilities = capabilitiesFor (endpoint.id, currentKnown ? current : AudioFormat {});
        }

        const auto verdict = judgeCompatibility (d.capabilities, candidate.sampleRate, candidate.bitDepth);
        d.compatibility = verdict.compatibility;
        d.reason = verdict.reason (candidate.sampleRate, candidate.bitDepth);
        const bool incompatible = verdict.compatibility == Compatibility::rateUnsupported
                                  || verdict.compatibility == Compatibility::depthUnsupported;

        if (! candidate.formatStandardization)
            d.action = ours ? DeviceAction::reenable : DeviceAction::none;
        else if (ours)
            d.action = incompatible && candidate.disableIncompatibleDevices ? DeviceAction::keepDisabled : DeviceAction::reenable;
        else if (verdict.compatibility == Compatibility::unknown)
            d.action = DeviceAction::unknown;
        else if (! incompatible)
            d.action = currentKnown && current.matches (candidate.sampleRate, candidate.bitDepth) ? DeviceAction::compliant
                                                                                                   : DeviceAction::apply;
        else
            d.action = candidate.disableIncompatibleDevices ? DeviceAction::disable : DeviceAction::ignore;
        out.push_back (std::move (d));
    }
    return out;
}

juce::String WatchdogEngine::renameDevice (const juce::String& endpointId, const juce::String& name)
{
    if (options_.admin == nullptr)
        return "devices cannot be renamed from here";
    const auto text = name.trim();
    if (text.isEmpty())
        return "the name is empty";
    const juce::ScopedLock sl (modifyLock_);
    juce::String before;
    options_.admin->getDescription (endpointId, before);
    if (const auto rc = options_.admin->setDescription (endpointId, text); failed (rc))
        return "Windows rejected the new name (" + hex (rc) + ")";
    juce::String after;
    if (failed (options_.admin->getDescription (endpointId, after)) || after != text)
        return "the new name did not stick";
    Logger::instance().info ("Device renamed by the user: '" + before + "' -> '" + text + "'. ID: " + endpointId);
    requestRescan();
    return {};
}

juce::String WatchdogEngine::enableDevice (const juce::String& endpointId)
{
    if (options_.admin == nullptr)
        return "devices cannot be enabled from here";
    const juce::ScopedLock sl (modifyLock_);
    auto record = deviceState().get (endpointId);
    EndpointState after = EndpointState::disabled;
    auto rc = options_.admin->setEnabled (endpointId, true);
    if (succeeded (rc))
        rc = options_.admin->getState (endpointId, after);
    if (failed (rc) || after == EndpointState::disabled)
        return "Windows did not enable the device (" + (failed (rc) ? hex (rc) : juce::String ("still disabled")) + ")";
    if (record)
    {
        // The user's decision: leave it enabled until the settings change.
        record->disabled = false;
        record->leftEnabled = true;
        deviceState().put (*record);
    }
    Logger::instance().info ("Device enabled again by the user" + (record ? " (" + record->name + ")" : juce::String())
                             + "; the incompatible-device policy leaves it enabled until the settings change. ID: " + endpointId);
    requestRescan();
    return {};
}
} // namespace audioslave
