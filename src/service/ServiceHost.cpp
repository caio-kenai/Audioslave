#include "platform/windows/WinCommon.h"
#include "service/ServiceHost.h"
#include "AudioslaveVersion.h"
#include "audio/windows/WindowsAudioDeviceWatcher.h"
#include "audio/windows/WindowsAudioEndpointEnumerator.h"
#include "audio/windows/WindowsAudioFormatPolicy.h"
#include "audio/windows/WindowsExclusiveModePolicy.h"
#include "common/Branding.h"
#include "ipc/PipeServer.h"
#include "logging/Logger.h"
#include "platform/windows/EventLog.h"
#include "platform/windows/Paths.h"
#include "platform/windows/ScopedComInit.h"
#include "platform/windows/WinError.h"

namespace audioslave
{
juce::String hostModeName (ServiceHost::Mode mode)
{
    switch (mode)
    {
        case ServiceHost::Mode::service:  return "service";
        case ServiceHost::Mode::portable: return "portable";
        case ServiceHost::Mode::console:  return "console";
    }
    return "console";
}

struct ServiceHost::Impl
{
    // Windows implementations, used when no test double is injected.
    win::WindowsAudioEndpointEnumerator windowsEnumerator;
    win::WindowsExclusiveModePolicy windowsExclusive;
    win::WindowsAudioFormatPolicy windowsFormat;

    std::unique_ptr<WatchdogEngine> engine;
    win::WindowsAudioDeviceWatcher watcher;
    std::unique_ptr<ipc::PipeServer> pipe;
    Configuration config;
};

ServiceHost::ServiceHost (Options options) : impl_ (std::make_unique<Impl>()), options_ (std::move (options))
{
    if (options_.configFile == juce::File())
        options_.configFile = paths::configFile();
    if (options_.pipeName.isEmpty())
        options_.pipeName = brand::controlPipeName;
}

ServiceHost::~ServiceHost() = default;

void ServiceHost::requestPause()
{
    pauseQueued_ = true;
    wake_.signal();
}

void ServiceHost::requestResume()
{
    resumeQueued_ = true;
    wake_.signal();
}

void ServiceHost::requestReload()
{
    reloadQueued_ = true;
    wake_.signal();
}

void ServiceHost::requestStop()
{
    stopQueued_ = true;
    wake_.signal();
}

int ServiceHost::run()
{
    const win::ScopedComInit com (COINIT_MULTITHREADED);
    if (! com.isUsable())
    {
        Logger::instance().error ("COM initialisation failed: " + win::hresultText (com.result()));
        return exitStartupFailed;
    }

    int exitCode = exitOk;
    try
    {
        exitCode = startAndServe();
    }
    catch (const std::exception& e)
    {
        Logger::instance().error ("Fatal error: " + juce::String (e.what()));
        if (options_.writeEventLog)
            win::writeEventLog (win::EventType::error, "Audioslave stopped by a fatal error: " + juce::String (e.what()));
        exitCode = exitStartupFailed;
    }
    catch (...)
    {
        Logger::instance().error ("Fatal error: unknown exception.");
        if (options_.writeEventLog)
            win::writeEventLog (win::EventType::error, "Audioslave stopped by an unknown fatal error.");
        exitCode = exitStartupFailed;
    }

    // Shutdown order: stop taking requests, stop notifications, stop the worker.
    stopping_ = true;
    if (impl_->pipe != nullptr)
        impl_->pipe->stop();
    impl_->watcher.stop();
    if (impl_->engine != nullptr)
    {
        impl_->engine->stop(); // reports STOPPING / STOPPED to the listener (SCM)
        impl_->engine->removeListener (this);
    }
    impl_->pipe.reset();
    impl_->engine.reset();
    started_ = false;

    auto& log = Logger::instance();
    log.info ("Audioslave stopped.");
    if (options_.writeEventLog && options_.mode == Mode::service)
        win::writeEventLog (win::EventType::information, "Audioslave stopped.");
    return exitCode;
}

int ServiceHost::startAndServe()
{
    startedAt_ = juce::Time::getCurrentTime();

    const auto loaded = loadConfiguration (options_.configFile, options_.mode == Mode::service);
    impl_->config = loaded.config;

    auto& log = Logger::instance();
    if (options_.configureLogging)
    {
        paths::ensureDirectory (paths::logsDir());
        const bool console = options_.mode == Mode::console;
        log.configure (impl_->config.enableLogging ? paths::serviceLogFile() : juce::File(), impl_->config.logLevel, console);
    }
    log.info ("Audioslave " AUDIOSLAVE_VERSION_STRING " starting (" + hostModeName (options_.mode) + ", pid "
              + juce::String (static_cast<juce::int64> (::GetCurrentProcessId())) + ", "
              + juce::SystemStats::getJUCEVersion() + ").");
    for (const auto& w : loaded.warnings)
        log.warn (w);
    logFeatures (impl_->config);

    auto& enumerator = options_.enumerator != nullptr ? *options_.enumerator : impl_->windowsEnumerator;
    auto& exclusive = options_.exclusiveStore != nullptr ? *options_.exclusiveStore : impl_->windowsExclusive;
    auto& format = options_.formatStore != nullptr ? *options_.formatStore : impl_->windowsFormat;

    WatchdogEngine::Options engineOptions;
    engineOptions.workerThreadScope = [] { return std::make_shared<win::ScopedComInit> (COINIT_MULTITHREADED); };
    impl_->engine = std::make_unique<WatchdogEngine> (enumerator, exclusive, format, impl_->config, engineOptions);
    impl_->engine->addListener (this);
    if (! impl_->engine->start())
    {
        log.error ("Could not start the watchdog worker thread.");
        return exitStartupFailed;
    }

    if (options_.watchDevices)
    {
        auto* engine = impl_->engine.get();
        if (const long hr = impl_->watcher.start ([engine] (const DeviceChange& c) { engine->onDeviceChange (c); });
            hr < 0)
            log.warn ("Device notifications unavailable (" + win::hresultText (hr) + "); relying on periodic checks.");
        else
            log.info ("Listening for device changes.");
    }

    if (options_.enableControlPipe)
    {
        impl_->pipe = std::make_unique<ipc::PipeServer> (
            options_.pipeName,
            options_.mode == Mode::service ? ipc::PipeServer::serviceSddl : ipc::PipeServer::portableSddl,
            [this] (const juce::MemoryBlock& request) { return handleRequest (request); });
        juce::String error;
        if (! impl_->pipe->start (error))
        {
            // Protection keeps running without the control channel.
            log.error ("Control channel unavailable: " + error);
            impl_->pipe.reset();
        }
        else
        {
            log.info ("Control channel listening on \\\\.\\pipe\\" + options_.pipeName + ".");
        }
    }

    started_ = true;
    log.info ("Audioslave is running.");
    if (options_.writeEventLog && options_.mode == Mode::service)
        win::writeEventLog (win::EventType::information, "Audioslave " AUDIOSLAVE_VERSION_STRING " started.");
    if (options_.onStarted)
        options_.onStarted();

    while (! stopQueued_.load())
    {
        wake_.wait (-1);
        processQueuedRequests();
    }
    log.info ("Audioslave stopping.");
    return exitOk;
}

void ServiceHost::processQueuedRequests()
{
    if (pauseQueued_.exchange (false))
        apply (ipc::Command::pause);
    if (resumeQueued_.exchange (false))
        apply (ipc::Command::resume);
    if (reloadQueued_.exchange (false))
        apply (ipc::Command::reload);
}

juce::String ServiceHost::apply (ipc::Command command)
{
    const juce::ScopedLock sl (controlLock_);
    auto* engine = impl_->engine.get();
    if (engine == nullptr || stopping_.load())
        return "the Audioslave host is stopping";

    auto& log = Logger::instance();
    switch (command)
    {
        case ipc::Command::status:
            return {};
        case ipc::Command::pause:
            engine->pause();
            return {};
        case ipc::Command::resume:
            // Resuming picks up configuration edits, then runs a full scan.
            reloadConfiguration (true);
            return {};
        case ipc::Command::reload:
            reloadConfiguration (false);
            log.info ("Configuration reloaded.");
            return {};
        case ipc::Command::scan:
            engine->requestFullScan();
            return engine->getState() == EngineState::paused ? juce::String ("monitoring is paused") : juce::String();
        case ipc::Command::stop:
            log.info ("Stop requested through the control channel.");
            requestStop();
            return {};
    }
    return "unknown command";
}

void ServiceHost::reloadConfiguration (bool resumeAfterwards)
{
    auto loaded = loadConfiguration (options_.configFile, false);
    auto& log = Logger::instance();
    for (const auto& w : loaded.warnings)
        log.warn (w);
    impl_->config = loaded.config;
    log.setLevel (loaded.config.logLevel);
    impl_->engine->setConfig (loaded.config);
    if (resumeAfterwards)
    {
        impl_->engine->resume();
    }
    else
    {
        logFeatures (loaded.config);
        impl_->engine->requestFullScan();
    }
    publishStatus();
}

void ServiceHost::logFeatures (const Configuration& cfg)
{
    auto& log = Logger::instance();
    log.info (juce::String ("Exclusive Mode Protection: ") + (cfg.exclusiveModeProtection ? "ENABLED" : "DISABLED"));
    log.info (juce::String ("Audio Format Standardization: ")
              + (cfg.formatStandardization ? "ENABLED (target " + describeFormatTarget (cfg) + ")" : juce::String ("DISABLED")));
    log.info (juce::String ("Playback: ") + (cfg.monitorPlayback ? "on" : "off") + " | Capture: "
              + (cfg.monitorCapture ? "on" : "off") + " | Check interval: " + juce::String (cfg.checkIntervalSeconds)
              + "s | Enforce: " + (cfg.enforce ? "on" : "report only"));
}

ipc::StatusSnapshot ServiceHost::snapshot() const
{
    ipc::StatusSnapshot s;
    s.version = AUDIOSLAVE_VERSION_STRING;
    s.pid = static_cast<int> (::GetCurrentProcessId());
    s.mode = hostModeName (options_.mode);
    s.startedAt = startedAt_;
    s.logsDir = paths::logsDir().getFullPathName();

    const auto cfg = impl_->engine != nullptr ? impl_->engine->getConfig() : impl_->config;
    s.exclusiveProtection = cfg.exclusiveModeProtection;
    s.formatStandardization = cfg.formatStandardization;
    s.enforce = cfg.enforce;
    s.formatTarget = describeFormatTarget (cfg);
    s.checkIntervalSeconds = static_cast<int> (cfg.checkIntervalSeconds);

    if (impl_->engine != nullptr)
    {
        const auto status = impl_->engine->getStatus();
        s.state = status.state;
        s.hasScanned = status.hasScanned;
        s.lastScanTime = status.lastScanTime;
        s.lastScan = status.lastScan;
        s.totalScans = status.totalScans;
        s.totalExclusiveFixes = status.totalExclusiveFixes;
        s.totalFormatChanges = status.totalFormatChanges;
        s.endpoints = status.endpoints;
    }
    if (impl_->pipe != nullptr)
        s.connectedClients = impl_->pipe->getNumConnections();
    return s;
}

juce::MemoryBlock ServiceHost::handleRequest (const juce::MemoryBlock& payload)
{
    const auto message = ipc::decode (payload);
    if (message.type != ipc::Message::Type::request)
        return ipc::encodeResponse (message.id, false,
                                    message.error.isNotEmpty() ? message.error : juce::String ("expected a request"), nullptr);

    const auto command = ipc::parseCommand (message.command);
    if (! command)
    {
        Logger::instance().warn ("Control channel: unknown command '" + message.command.substring (0, 32) + "'.");
        return ipc::encodeResponse (message.id, false, "unknown command: " + message.command.substring (0, 32), nullptr);
    }
    if (*command != ipc::Command::status)
        Logger::instance().info ("Control channel: " + ipc::commandName (*command) + " requested.");

    const auto error = apply (*command);
    const auto status = snapshot();
    return ipc::encodeResponse (message.id, error.isEmpty(), error, &status);
}

void ServiceHost::publishStatus()
{
    if (impl_->pipe != nullptr && impl_->pipe->getNumConnections() > 0)
        impl_->pipe->broadcast (ipc::encodeStatusEvent (snapshot()));
}

void ServiceHost::engineStateChanged (EngineState state)
{
    if (options_.onStateChanged)
        options_.onStateChanged (state);
    publishStatus();
}

void ServiceHost::scanCompleted (const ScanReport& report)
{
    if (options_.writeEventLog)
    {
        if (report.exclusiveFixed > 0)
            win::writeEventLog (win::EventType::warning,
                                "Audioslave re-disabled exclusive mode on " + juce::String (report.exclusiveFixed) + " endpoint(s).");
        if (report.formatApplied > 0)
            win::writeEventLog (win::EventType::information,
                                "Audioslave set the default format (" + describeFormatTarget (impl_->engine->getConfig())
                                    + ") on " + juce::String (report.formatApplied) + " endpoint(s).");
    }
    publishStatus();
}
} // namespace audioslave
