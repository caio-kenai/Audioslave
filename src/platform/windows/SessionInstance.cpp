#include "platform/windows/WinCommon.h"
#include "platform/windows/SessionInstance.h"
#include "platform/windows/WinHandles.h"
#include "common/Branding.h"

namespace audioslave::win
{
namespace
{
bool signalEvent (const wchar_t* name)
{
    UniqueHandle event (::OpenEventW (EVENT_MODIFY_STATE, FALSE, name));
    return event && ::SetEvent (event.get()) != FALSE;
}
} // namespace

struct SessionInstance::Impl final : private juce::Thread
{
    Impl() : juce::Thread ("Audioslave session instance")
    {
        mutex.reset (::CreateMutexW (nullptr, FALSE, brand::trayMutexName));
        primary = mutex && ::GetLastError() != ERROR_ALREADY_EXISTS;
        if (! primary)
            return;

        showEvent.reset (::CreateEventW (nullptr, FALSE, FALSE, brand::trayShowEventName));
        quitEvent.reset (::CreateEventW (nullptr, FALSE, FALSE, brand::trayQuitEventName));
        stopEvent.reset (::CreateEventW (nullptr, TRUE, FALSE, nullptr));
    }

    ~Impl() override { stop(); }

    void start (std::function<void()> show, std::function<void()> quit)
    {
        if (! primary || ! showEvent || ! quitEvent || ! stopEvent || isThreadRunning())
            return;
        onShow = std::move (show);
        onQuit = std::move (quit);
        ::ResetEvent (stopEvent.get());
        startThread();
    }

    void stop()
    {
        if (stopEvent)
            ::SetEvent (stopEvent.get());
        waitForThreadToExit (-1);
    }

    void run() override
    {
        HANDLE handles[] = { stopEvent.get(), showEvent.get(), quitEvent.get() };
        for (;;)
        {
            const DWORD r = ::WaitForMultipleObjects (3, handles, FALSE, INFINITE);
            if (r == WAIT_OBJECT_0 + 1 && onShow)
                onShow();
            else if (r == WAIT_OBJECT_0 + 2 && onQuit)
                onQuit();
            else
                return; // stop requested or wait failure
        }
    }

    UniqueHandle mutex, showEvent, quitEvent, stopEvent;
    bool primary = false;
    std::function<void()> onShow, onQuit;
};

SessionInstance::SessionInstance() : impl_ (std::make_unique<Impl>()) {}
SessionInstance::~SessionInstance() = default;

bool SessionInstance::isPrimary() const noexcept { return impl_->primary; }

void SessionInstance::startListening (std::function<void()> onShow, std::function<void()> onQuit)
{
    impl_->start (std::move (onShow), std::move (onQuit));
}

void SessionInstance::stopListening() { impl_->stop(); }

bool SessionInstance::signalShow()
{
    // This (just launched, foreground) process lets the running tray bring
    // its window to the front.
    ::AllowSetForegroundWindow (ASFW_ANY);
    return signalEvent (brand::trayShowEventName);
}

bool SessionInstance::signalQuit()
{
    return signalEvent (brand::trayQuitEventName);
}
} // namespace audioslave::win
