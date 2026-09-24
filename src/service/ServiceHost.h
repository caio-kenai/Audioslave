#pragma once
// ServiceHost: runs the watchdog. The same object backs
//   - the Windows service (WindowsService, under the SCM),
//   - the portable host (a thread of the tray when no service is installed),
//   - the console mode (Audioslave run).
//
// It owns the engine, the device watcher and the control pipe, and applies
// every control request - from the SCM, the pipe or Ctrl+C - through one
// code path, so the SCM state, the engine state and what the tray shows
// never disagree.

#include "audio/AudioInterfaces.h"
#include "config/Configuration.h"
#include "core/WatchdogEngine.h"
#include "ipc/Protocol.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace audioslave
{
class ServiceHost final : private WatchdogEngine::Listener
{
public:
    enum class Mode
    {
        service,
        portable,
        console
    };

    struct Options
    {
        Mode mode = Mode::console;
        juce::File configFile;              // default: C:\ProgramData\Audioslave\config.ini
        juce::File deviceStateFile;         // default: devices.json next to the configuration
        juce::String pipeName;              // default: Audioslave.Control
        bool configureLogging = true;       // false in tests
        bool writeEventLog = true;          // false in tests
        bool watchDevices = true;           // IMMNotificationClient
        bool enableControlPipe = true;

        // Test seams; null = the Windows implementations.
        IAudioEndpointEnumerator* enumerator = nullptr;
        IExclusiveModeStore* exclusiveStore = nullptr;
        IAudioFormatStore* formatStore = nullptr;
        IEndpointAdmin* endpointAdmin = nullptr;

        // Engine state changes (the service reports them to the SCM).
        std::function<void (EngineState)> onStateChanged;
        // Called once everything is up, before waiting for requests.
        std::function<void()> onStarted;
    };

    static constexpr int exitOk = 0;
    static constexpr int exitStartupFailed = 1066; // ERROR_SERVICE_SPECIFIC_ERROR: SCM recovery restarts us

    explicit ServiceHost (Options options);
    ~ServiceHost() override;

    ServiceHost (const ServiceHost&) = delete;
    ServiceHost& operator= (const ServiceHost&) = delete;

    // Starts everything, blocks until a stop is requested, shuts down cleanly.
    // Must be called on one thread for its whole duration (COM apartment).
    int run();

    // Thread-safe, non-blocking requests (SCM handler, console handler).
    void requestPause();
    void requestResume();
    void requestReload();
    void requestStop();

    // Applies a command synchronously (pipe clients). Returns an error text
    // when the command was not accepted; `result` receives the command's
    // result (ANALYZE, CONFIGURE).
    juce::String apply (ipc::Command command);
    juce::String apply (ipc::Command command, const juce::var& args, juce::var& result);

    [[nodiscard]] ipc::StatusSnapshot snapshot() const;
    [[nodiscard]] bool isStarted() const noexcept { return started_.load(); }

private:
    void engineStateChanged (EngineState state) override;
    void scanCompleted (const ScanReport& report) override;

    juce::MemoryBlock handleRequest (const juce::MemoryBlock& payload);
    void reloadConfiguration (bool resumeAfterwards);
    juce::String configure (const ipc::AudioSettings& settings, juce::var& result);
    juce::String analyze (const ipc::AudioSettings& settings, juce::var& result);
    juce::String rename (const juce::String& endpointId, const juce::String& name);
    Configuration withSettings (Configuration config, const ipc::AudioSettings& settings) const;
    void logFeatures (const Configuration& config);
    void publishStatus();
    int startAndServe();
    void processQueuedRequests();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    Options options_;
    std::atomic<bool> started_ { false };
    std::atomic<bool> stopping_ { false };
    std::atomic<bool> pauseQueued_ { false }, resumeQueued_ { false }, reloadQueued_ { false }, stopQueued_ { false };
    juce::WaitableEvent wake_;
    juce::CriticalSection controlLock_;
    juce::Time startedAt_;
};

juce::String hostModeName (ServiceHost::Mode mode);
} // namespace audioslave
