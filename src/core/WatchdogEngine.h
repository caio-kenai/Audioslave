#pragma once
// The watchdog: enumerates the endpoints and enforces the policies on a
// worker thread (juce::Thread), periodically and promptly after device
// notifications.
//
//   - Only RUNNING modifies devices. pause() returns only after any change in
//     flight has finished, so "paused" really means nothing is being touched.
//   - Notifications are debounced (bursts settle into one pass).
//   - Triggered scans respect the per-endpoint failure backoff; periodic and
//     requested full scans retry everything.
//   - The thread is always stopped cooperatively (never killed).

#include "audio/AudioInterfaces.h"
#include "audio/models/DeviceChange.h"
#include "config/Configuration.h"
#include "core/EndpointMemory.h"
#include "core/EngineStatus.h"
#include "core/ExclusiveModePolicy.h"
#include "core/FormatPolicy.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace audioslave
{
class WatchdogEngine final : private juce::Thread
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        // Called from the thread that changed the state.
        virtual void engineStateChanged (EngineState) {}
        // Called on the worker thread after every scan pass.
        virtual void scanCompleted (const ScanReport&) {}
    };

    // Called on the worker thread when it starts; the returned object lives
    // until the worker exits (the service uses it to initialise COM).
    using ThreadScopeFactory = std::function<std::shared_ptr<void>()>;

    struct Options
    {
        ThreadScopeFactory workerThreadScope;
        EndpointMemory::Clock clock;
        int debounceMs = 400;
        int enumerationRetryMs = 5000; // retry sooner while the audio stack is not reachable (boot)
    };

    WatchdogEngine (IAudioEndpointEnumerator& enumerator, IExclusiveModeStore& exclusiveStore,
                    IAudioFormatStore& formatStore, Configuration config, Options options = {});
    ~WatchdogEngine() override;

    // Starts the worker (first pass immediately). Returns false if the thread
    // could not be created.
    bool start();

    // Stops and joins the worker. Idempotent.
    void stop();

    // Suspends every modification (device events keep arriving). Returns when
    // no change is in progress.
    void pause();
    // Leaves PAUSED, forgets backoff/notices and schedules a full scan.
    void resume();
    [[nodiscard]] EngineState getState() const noexcept { return state_.load(); }

    void requestRescan();
    void requestFullScan();

    // Feeds a watcher notification: logs it and schedules a rescan. Quick,
    // non-blocking: safe to call from audio-service threads.
    void onDeviceChange (const DeviceChange& change);

    // One synchronous pass (also used by the `scan` CLI and by tests).
    ScanReport scanOnce (ScanKind kind = ScanKind::full);

    [[nodiscard]] Configuration getConfig() const;
    // Takes effect on the next pass; forgets backoff and notices.
    void setConfig (const Configuration& config);

    [[nodiscard]] EngineStatus getStatus() const;

    void addListener (Listener* listener) { listeners_.add (listener); }
    void removeListener (Listener* listener) { listeners_.remove (listener); }

private:
    void run() override;
    void setState (EngineState newState);
    [[nodiscard]] bool canModify() const noexcept
    {
        return state_.load() == EngineState::running && ! pauseRequested_.load();
    }
    void notice (const juce::String& key, const juce::String& message);

    IAudioEndpointEnumerator& enumerator_;
    ExclusiveModePolicy exclusivePolicy_;
    FormatPolicy formatPolicy_;
    Options options_;
    EndpointMemory memory_;

    mutable juce::CriticalSection configLock_;
    Configuration config_;

    std::atomic<EngineState> state_ { EngineState::running };
    std::atomic<bool> rescanPending_ { false };
    std::atomic<bool> fullScanPending_ { false };
    // Raised by pause() before it waits for modifyLock_: CriticalSection is
    // not fair, so without it a running pass could keep re-taking the lock
    // for its next endpoint while pause() waits.
    std::atomic<bool> pauseRequested_ { false };

    // Held around each endpoint's inspect-and-fix step and by pause().
    juce::CriticalSection modifyLock_;

    mutable juce::CriticalSection statusLock_;
    EngineStatus status_;

    juce::ThreadSafeListenerList<Listener> listeners_;
};
} // namespace audioslave
