#include "core/WatchdogEngine.h"
#include "logging/Logger.h"

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
    {
        const juce::ScopedLock sl (configLock_);
        config_ = config;
    }
    memory_.clear();
}

EngineStatus WatchdogEngine::getStatus() const
{
    const juce::ScopedLock sl (statusLock_);
    auto copy = status_;
    copy.state = state_.load();
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
    // report it afresh.
    memory_.forgetEndpoint (change.endpointId);
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

ScanReport WatchdogEngine::scanOnce (ScanKind kind)
{
    ScanReport report;
    report.kind = kind;
    auto& log = Logger::instance();

    if (! canModify())
    {
        report.paused = true;
        return report;
    }

    const auto cfg = getConfig();
    std::vector<AudioEndpoint> endpoints;
    const auto rc = enumerator_.enumerate (endpoints);
    std::vector<EndpointStatus> snapshot;

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
                switch (out.result)
                {
                    case FormatResult::compliant:
                        ++report.formatCompliant;
                        entry.format = describeFormat (out.before);
                        break;
                    case FormatResult::applied:
                        ++report.formatApplied;
                        memory_.clearFailure (endpoint.id);
                        entry.format = describeFormat (out.applied);
                        log.info ("[" + name + "] default format changed: " + describeFormat (out.before) + " -> " + target);
                        break;
                    case FormatResult::unsupported:
                        ++report.formatUnsupported;
                        entry.format = describeFormat (out.before);
                        notice (key, "[" + name + "] format standardization skipped. Requested: " + target
                                         + ". Device supports: " + out.supported + ". Current: " + describeFormat (out.before)
                                         + ". Reason: requested format is not supported.");
                        break;
                    case FormatResult::unknown:
                        ++report.formatFailed;
                        notice (key, "[" + name + "] format standardization skipped: the current or supported formats "
                                     "could not be determined (" + hex (out.code) + ").");
                        break;
                    case FormatResult::writeFailed:
                        ++report.formatFailed;
                        memory_.recordFailure (endpoint.id);
                        entry.format = describeFormat (out.before);
                        log.error ("[" + name + "] FAILED to set the default format " + target + " (" + hex (out.code) + ")");
                        break;
                    case FormatResult::verifyFailed:
                        ++report.formatFailed;
                        memory_.recordFailure (endpoint.id);
                        log.error ("[" + name + "] default format " + target + " did not stick (verify failed)");
                        break;
                    case FormatResult::skipped:
                        ++report.formatSkipped;
                        entry.format = describeFormat (out.before);
                        log.debug ("[" + name + "] format is " + describeFormat (out.before) + ", target " + target
                                   + " not applied (" + (backoff ? "recent failure" : "enforcement off") + ")");
                        break;
                }
                if (out.result == FormatResult::compliant || out.result == FormatResult::applied)
                    memory_.clearNotice (key);
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
                         + " unsupported | errors: " + juce::String (report.errors());
    if (report.changedSomething())
        log.info (summary);
    else
        log.debug (summary);

    {
        const juce::ScopedLock sl (statusLock_);
        status_.hasScanned = true;
        status_.lastScan = report;
        status_.lastScanTime = juce::Time::getCurrentTime();
        ++status_.totalScans;
        status_.totalExclusiveFixes += report.exclusiveFixed;
        status_.totalFormatChanges += report.formatApplied;
        if (! report.paused)
            status_.endpoints = std::move (snapshot);
    }
    listeners_.call ([&report] (Listener& l) { l.scanCompleted (report); });
    return report;
}
} // namespace audioslave
