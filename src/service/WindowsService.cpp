#include "platform/windows/WinCommon.h"
#include "service/WindowsService.h"
#include "common/Branding.h"
#include "logging/Logger.h"
#include "service/ServiceHost.h"

#include <mutex>

namespace audioslave
{
namespace
{
// The control handler runs on the SCM dispatcher thread while ServiceMain
// runs on its own thread: every status change is serialised.
struct ServiceState
{
    std::mutex mutex;
    SERVICE_STATUS_HANDLE handle = nullptr;
    SERVICE_STATUS status {};
    DWORD checkPoint = 1;

    // The handler only calls ServiceHost's non-blocking request*() methods
    // while holding hostMutex; serviceMain clears `host` under the same mutex
    // before the host is destroyed, so the handler never sees a dangling host.
    std::mutex hostMutex;
    ServiceHost* host = nullptr;
};

ServiceState& state()
{
    static ServiceState s;
    return s;
}

void reportState (DWORD current, DWORD exitCode = NO_ERROR, DWORD waitHintMs = 0)
{
    auto& s = state();
    const std::lock_guard<std::mutex> lk (s.mutex);
    s.status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s.status.dwCurrentState = current;
    s.status.dwControlsAccepted = 0;
    if (current == SERVICE_RUNNING || current == SERVICE_PAUSED)
        s.status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN | SERVICE_ACCEPT_PAUSE_CONTINUE
                                      | SERVICE_ACCEPT_PARAMCHANGE;
    s.status.dwWin32ExitCode = exitCode;
    s.status.dwServiceSpecificExitCode = 0;
    const bool pending = current == SERVICE_START_PENDING || current == SERVICE_STOP_PENDING
                         || current == SERVICE_PAUSE_PENDING || current == SERVICE_CONTINUE_PENDING;
    s.status.dwCheckPoint = pending ? s.checkPoint++ : 0;
    if (! pending)
        s.checkPoint = 1;
    s.status.dwWaitHint = waitHintMs;
    if (s.handle != nullptr)
        ::SetServiceStatus (s.handle, &s.status);
}

DWORD currentState()
{
    auto& s = state();
    const std::lock_guard<std::mutex> lk (s.mutex);
    return s.status.dwCurrentState;
}

DWORD WINAPI controlHandler (DWORD control, DWORD, LPVOID, LPVOID)
{
    auto& s = state();
    const std::lock_guard<std::mutex> hostLock (s.hostMutex);
    auto* host = s.host;
    const DWORD current = currentState();
    const bool active = current == SERVICE_RUNNING || current == SERVICE_PAUSED;

    switch (control)
    {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        case SERVICE_CONTROL_PRESHUTDOWN:
            if (active && host != nullptr)
            {
                reportState (SERVICE_STOP_PENDING, NO_ERROR, 15000);
                host->requestStop();
            }
            return NO_ERROR;
        case SERVICE_CONTROL_PAUSE:
            if (current == SERVICE_RUNNING && host != nullptr)
            {
                reportState (SERVICE_PAUSE_PENDING, NO_ERROR, 5000);
                host->requestPause();
            }
            return NO_ERROR;
        case SERVICE_CONTROL_CONTINUE:
            if (current == SERVICE_PAUSED && host != nullptr)
            {
                reportState (SERVICE_CONTINUE_PENDING, NO_ERROR, 5000);
                host->requestResume();
            }
            return NO_ERROR;
        case SERVICE_CONTROL_PARAMCHANGE:
            if (active && host != nullptr)
                host->requestReload();
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI serviceMain (DWORD, LPWSTR*)
{
    auto& s = state();
    s.handle = ::RegisterServiceCtrlHandlerExW (brand::serviceName, controlHandler, nullptr);
    if (s.handle == nullptr)
        return;

    reportState (SERVICE_START_PENDING, NO_ERROR, 30000);

    ServiceHost::Options options;
    options.mode = ServiceHost::Mode::service;
    options.onStateChanged = [] (EngineState engineState)
    {
        // Pause / continue may come from the SCM or from the control pipe:
        // either way the SCM ends up with the engine's real state.
        const DWORD current = currentState();
        if (engineState == EngineState::paused && current != SERVICE_STOP_PENDING)
            reportState (SERVICE_PAUSED);
        else if (engineState == EngineState::running && current != SERVICE_START_PENDING && current != SERVICE_STOP_PENDING)
            reportState (SERVICE_RUNNING);
        else if (engineState == EngineState::stopping && current != SERVICE_STOP_PENDING)
            reportState (SERVICE_STOP_PENDING, NO_ERROR, 15000);
    };
    options.onStarted = [] { reportState (SERVICE_RUNNING); };

    int exitCode = ServiceHost::exitOk;
    {
        ServiceHost host (options);
        {
            const std::lock_guard<std::mutex> lk (s.hostMutex);
            s.host = &host;
        }
        exitCode = host.run();
        const std::lock_guard<std::mutex> lk (s.hostMutex);
        s.host = nullptr;
    }

    Logger::instance().close();
    // Once SERVICE_STOPPED is reported the process may be torn down at any
    // moment: everything is released before this call.
    reportState (SERVICE_STOPPED, static_cast<DWORD> (exitCode));
}
} // namespace

bool runWindowsService()
{
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR> (brand::serviceName), serviceMain },
        { nullptr, nullptr },
    };
    return ::StartServiceCtrlDispatcherW (table) != FALSE;
}
} // namespace audioslave
