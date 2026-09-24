#pragma once
// Device-change notifications through IMMNotificationClient.
//
// This stays native on purpose: juce::AudioIODeviceType::Listener only fires
// when the *list* of devices changes, while Audioslave must react when an
// endpoint *property* changes (Windows or a driver re-enabling exclusive
// mode, the default format being edited in the Sound panel).
//
// Lifetime: the COM object is heap-allocated and reference counted by COM.
// It forwards to a shared sink guarded by a lock; stop() detaches the sink
// under that lock, so once stop() returns no callback can reach the owner
// (Audio Watchdog's watcher could still be running a callback after Stop()).

#include "audio/models/DeviceChange.h"

#include <functional>
#include <memory>

namespace audioslave::win
{
class WindowsAudioDeviceWatcher
{
public:
    using Callback = std::function<void (const DeviceChange&)>;

    WindowsAudioDeviceWatcher();
    ~WindowsAudioDeviceWatcher();

    WindowsAudioDeviceWatcher (const WindowsAudioDeviceWatcher&) = delete;
    WindowsAudioDeviceWatcher& operator= (const WindowsAudioDeviceWatcher&) = delete;

    // Registers for notifications. COM (MTA) must be initialised on the
    // calling thread. `callback` runs on audio-service threads: it must be
    // quick and must not block. Returns an HRESULT.
    long start (Callback callback);

    // Unregisters and waits for an in-flight callback. Idempotent.
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace audioslave::win
