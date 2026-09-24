#include "platform/windows/WinCommon.h"
#include "audio/windows/WindowsAudioDeviceWatcher.h"
#include "audio/windows/ComHelpers.h"

#include <atomic>

namespace audioslave::win
{
namespace
{
struct Sink
{
    juce::CriticalSection lock;
    WindowsAudioDeviceWatcher::Callback callback;

    void deliver (DeviceChange::Kind kind, LPCWSTR id, DWORD state = DEVICE_STATE_ACTIVE)
    {
        const juce::ScopedLock sl (lock);
        if (! callback)
            return;

        DeviceChange change;
        change.kind = kind;
        change.endpointId = id != nullptr ? juce::String (id) : juce::String();
        switch (state)
        {
            case DEVICE_STATE_DISABLED:   change.newState = EndpointState::disabled; break;
            case DEVICE_STATE_NOTPRESENT: change.newState = EndpointState::notPresent; break;
            case DEVICE_STATE_UNPLUGGED:  change.newState = EndpointState::unplugged; break;
            default:                      change.newState = EndpointState::active; break;
        }
        try
        {
            callback (change);
        }
        catch (...)
        {
            // Never let an exception cross the COM boundary into the audio service.
        }
    }
};

// Heap-allocated COM object with a thread-safe reference count
// (juce::ComBaseClassHelper's count is not atomic, and IMMNotificationClient
// is called from several audio-service threads).
class NotificationClient final : public IMMNotificationClient
{
public:
    explicit NotificationClient (std::shared_ptr<Sink> sink) : sink_ (std::move (sink)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** object) override
    {
        if (object == nullptr)
            return E_POINTER;
        if (riid == __uuidof (IUnknown) || riid == __uuidof (IMMNotificationClient))
        {
            *object = static_cast<IMMNotificationClient*> (this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG remaining = --refCount_;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged (LPCWSTR id, DWORD state) override
    {
        sink_->deliver (DeviceChange::Kind::stateChanged, id, state);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded (LPCWSTR id) override
    {
        sink_->deliver (DeviceChange::Kind::added, id);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved (LPCWSTR id) override
    {
        sink_->deliver (DeviceChange::Kind::removed, id);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged (EDataFlow, ERole role, LPCWSTR id) override
    {
        // One notification per role; the console role is enough.
        if (role == eConsole)
            sink_->deliver (DeviceChange::Kind::defaultChanged, id);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged (LPCWSTR id, const PROPERTYKEY) override
    {
        sink_->deliver (DeviceChange::Kind::propertyChanged, id);
        return S_OK;
    }

private:
    ~NotificationClient() = default;

    std::atomic<ULONG> refCount_ { 1 };
    std::shared_ptr<Sink> sink_;
};
} // namespace

struct WindowsAudioDeviceWatcher::Impl
{
    std::shared_ptr<Sink> sink = std::make_shared<Sink>();
    juce::ComSmartPtr<IMMDeviceEnumerator> enumerator;
    juce::ComSmartPtr<NotificationClient> client;
};

WindowsAudioDeviceWatcher::WindowsAudioDeviceWatcher() : impl_ (std::make_unique<Impl>()) {}

WindowsAudioDeviceWatcher::~WindowsAudioDeviceWatcher()
{
    stop();
}

long WindowsAudioDeviceWatcher::start (Callback callback)
{
    stop();

    juce::ComSmartPtr<IMMDeviceEnumerator> enumerator;
    if (const HRESULT hr = createDeviceEnumerator (enumerator); FAILED (hr))
        return hr;

    {
        const juce::ScopedLock sl (impl_->sink->lock);
        impl_->sink->callback = std::move (callback);
    }
    juce::ComSmartPtr<NotificationClient> client (new NotificationClient (impl_->sink), juce::IncrementRef::no);
    if (const HRESULT hr = enumerator->RegisterEndpointNotificationCallback (client); FAILED (hr))
    {
        const juce::ScopedLock sl (impl_->sink->lock);
        impl_->sink->callback = nullptr;
        return hr;
    }
    impl_->enumerator = enumerator;
    impl_->client = client;
    return S_OK;
}

void WindowsAudioDeviceWatcher::stop()
{
    {
        // Waits for a callback that is running right now; later ones are dropped.
        const juce::ScopedLock sl (impl_->sink->lock);
        impl_->sink->callback = nullptr;
    }
    if (impl_->enumerator != nullptr && impl_->client != nullptr)
        impl_->enumerator->UnregisterEndpointNotificationCallback (impl_->client);
    impl_->client = nullptr;
    impl_->enumerator = nullptr;
}

bool WindowsAudioDeviceWatcher::isRunning() const noexcept
{
    return impl_->client != nullptr;
}
} // namespace audioslave::win
