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
//
// Incompatible-device policy (format standardization on): a device that does
// not support the chosen format is only reported, or - when the user enabled
// and confirmed it - disabled (IEndpointAdmin). Audioslave only ever
// re-enables devices it disabled itself (DeviceStateStore): when they become
// compatible or the policy is turned off. The policy is kept like any other:
// an incompatible device that is enabled again (in the Sound panel, by Windows
// or by its driver) is disabled again at once. Only a real loop - more than
// maxDisablesPerWindow times within loopWindowMs, e.g. a driver re-creating
// the device over and over - makes it wait until that window has passed.
// Devices that are not active (disconnected) or report no formats at all are
// never judged incompatible.

#include "audio/AudioInterfaces.h"
#include "audio/models/DeviceChange.h"
#include "config/Configuration.h"
#include "core/DeviceStateStore.h"
#include "core/EndpointMemory.h"
#include "core/EngineStatus.h"
#include "core/ExclusiveModePolicy.h"
#include "core/FormatPolicy.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
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

        // Enables disabling / re-enabling / renaming devices (null: report only).
        IEndpointAdmin* admin = nullptr;
        // Devices disabled by Audioslave (null: kept in memory only).
        DeviceStateStore* deviceState = nullptr;
        // Wall clock in ms since 1970 (the disable history); injectable for tests.
        std::function<juce::int64()> wallClock;
    };

    static constexpr juce::int64 loopWindowMs = 10 * 60 * 1000;
    static constexpr int maxDisablesPerWindow = 5;
    static constexpr size_t maxEvents = 32;

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
    // `interactive`: run for a change the user is applying right now.
    ScanReport scanOnce (ScanKind kind = ScanKind::full, bool interactive = false);

    // Read-only preview of what `candidate` would do to every device (the
    // settings screen shows it before anything is applied).
    std::vector<DeviceReport> analyze (const Configuration& candidate);

    // "Verificar agora": ask every driver for its formats again.
    void forgetCapabilities();

    // Writes the endpoint's name now (the configuration keeps it enforced).
    // Returns an error text, empty on success.
    juce::String renameDevice (const juce::String& endpointId, const juce::String& name);

    // The user re-enabled a device Audioslave had disabled: enable it and
    // leave it enabled until the configuration changes. Error text or empty.
    juce::String enableDevice (const juce::String& endpointId);

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

    DeviceStateStore& deviceState() { return options_.deviceState != nullptr ? *options_.deviceState : ownedState_; }
    juce::int64 now() const { return options_.wallClock ? options_.wallClock() : juce::Time::currentTimeMillis(); }
    FormatCapabilities capabilitiesFor (const juce::String& endpointId, const AudioFormat& current);
    void handleIncompatible (const AudioEndpoint& endpoint, const Configuration& cfg, bool enforce, bool interactive,
                             ScanReport& report, DeviceReport& device);
    void handleDisabledByAudioslave (const AudioEndpoint& endpoint, const DisabledDeviceRecord& record,
                                     const Configuration& cfg, bool enforce, bool interactive, ScanReport& report,
                                     DeviceReport& device);
    void restoreName (const AudioEndpoint& endpoint, const juce::String& wanted, bool enforce, ScanReport& report,
                      EndpointStatus& entry);
    void addEvent (DeviceAction action, const DeviceReport& device, bool interactive);

    IAudioEndpointEnumerator& enumerator_;
    ExclusiveModePolicy exclusivePolicy_;
    FormatPolicy formatPolicy_;
    Options options_;
    EndpointMemory memory_;
    DeviceStateStore ownedState_;

    juce::CriticalSection capabilitiesLock_;
    std::map<juce::String, FormatCapabilities> capabilities_;

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
    // One pass at a time: the worker's and one run for the user (settings
    // applied from the window) must not interleave, or a pass that enumerated
    // before a device was disabled would take it for "enabled again".
    juce::CriticalSection scanLock_;

    mutable juce::CriticalSection statusLock_;
    EngineStatus status_;
    std::deque<DeviceEvent> events_;
    juce::int64 nextEventSequence_ = 1;

    juce::ThreadSafeListenerList<Listener> listeners_;
};
} // namespace audioslave
