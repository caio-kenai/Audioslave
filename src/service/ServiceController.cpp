#include "platform/windows/WinCommon.h"
#include "service/ServiceController.h"
#include "common/Branding.h"
#include "platform/windows/WinError.h"
#include "platform/windows/WinHandles.h"

#include <sddl.h>
#include <winsvc.h>

#include <vector>

namespace audioslave::scm
{
namespace
{
using win::ServiceHandle;

const wchar_t* nameOrDefault (const wchar_t* serviceName)
{
    return serviceName != nullptr ? serviceName : brand::serviceName;
}

State fromNative (DWORD state)
{
    switch (state)
    {
        case SERVICE_STOPPED:          return State::stopped;
        case SERVICE_START_PENDING:    return State::startPending;
        case SERVICE_STOP_PENDING:     return State::stopPending;
        case SERVICE_RUNNING:          return State::running;
        case SERVICE_CONTINUE_PENDING: return State::continuePending;
        case SERVICE_PAUSE_PENDING:    return State::pausePending;
        case SERVICE_PAUSED:           return State::paused;
        default:                       return State::unknown;
    }
}

ServiceHandle openService (const wchar_t* name, DWORD access, DWORD& errorOut)
{
    ServiceHandle manager (::OpenSCManagerW (nullptr, nullptr, SC_MANAGER_CONNECT));
    if (! manager)
    {
        errorOut = ::GetLastError();
        return {};
    }
    ServiceHandle service (::OpenServiceW (manager.get(), name, access));
    if (! service)
        errorOut = ::GetLastError();
    return service;
}

// Polls until the service reaches `target` (or leaves a pending state).
bool waitForState (SC_HANDLE service, DWORD target, int timeoutMs, SERVICE_STATUS& status)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (status.dwCurrentState != target)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep (250);
        if (! ::QueryServiceStatus (service, &status))
            return false;
    }
    return true;
}

bool control (DWORD code, DWORD target, DWORD access, juce::String& error)
{
    DWORD openError = 0;
    auto service = openService (brand::serviceName, access | SERVICE_QUERY_STATUS, openError);
    if (! service)
    {
        error = win::win32ErrorText (openError);
        return false;
    }
    SERVICE_STATUS status {};
    if (! ::ControlService (service.get(), code, &status))
    {
        error = win::lastErrorText();
        return false;
    }
    if (! waitForState (service.get(), target, 10000, status))
    {
        error = "the service did not reach the requested state (now " + stateName (fromNative (status.dwCurrentState)) + ")";
        return false;
    }
    return true;
}
} // namespace

juce::String stateName (State state)
{
    switch (state)
    {
        case State::notInstalled:    return "Not installed";
        case State::stopped:         return "Stopped";
        case State::startPending:    return "Starting";
        case State::stopPending:     return "Stopping";
        case State::running:         return "Running";
        case State::continuePending: return "Continuing";
        case State::pausePending:    return "Pausing";
        case State::paused:          return "Paused";
        case State::unknown:         break;
    }
    return "Unknown";
}

Status query (const wchar_t* serviceName)
{
    Status result;
    DWORD openError = 0;
    auto service = openService (nameOrDefault (serviceName), SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG, openError);
    if (! service)
    {
        result.state = openError == ERROR_SERVICE_DOES_NOT_EXIST ? State::notInstalled : State::unknown;
        if (openError != ERROR_SERVICE_DOES_NOT_EXIST)
            result.error = win::win32ErrorText (openError);
        return result;
    }

    SERVICE_STATUS_PROCESS process {};
    DWORD needed = 0;
    if (::QueryServiceStatusEx (service.get(), SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE> (&process),
                                sizeof (process), &needed))
    {
        result.state = fromNative (process.dwCurrentState);
        result.processId = process.dwProcessId;
        result.acceptsStop = (process.dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0;
        result.acceptsPauseContinue = (process.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) != 0;
        result.acceptsPreshutdown = (process.dwControlsAccepted & SERVICE_ACCEPT_PRESHUTDOWN) != 0;
    }
    else
    {
        result.state = State::unknown;
        result.error = win::lastErrorText();
    }

    needed = 0;
    ::QueryServiceConfigW (service.get(), nullptr, 0, &needed);
    if (needed > 0)
    {
        std::vector<BYTE> buffer (needed);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*> (buffer.data());
        if (::QueryServiceConfigW (service.get(), config, needed, &needed))
        {
            switch (config->dwStartType)
            {
                case SERVICE_AUTO_START:   result.startType = "Automatic"; break;
                case SERVICE_DEMAND_START: result.startType = "Manual"; break;
                case SERVICE_DISABLED:     result.startType = "Disabled"; break;
                default:                   result.startType = "Other"; break;
            }
            if (config->lpBinaryPathName != nullptr)
                result.binaryPath = config->lpBinaryPathName;
        }
    }
    return result;
}

bool exists (const wchar_t* serviceName)
{
    return query (serviceName).state != State::notInstalled;
}

bool install (const juce::String& commandLine, juce::String& error, juce::StringArray& warnings)
{
    ServiceHandle manager (::OpenSCManagerW (nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE));
    if (! manager)
    {
        error = win::lastErrorText();
        return false;
    }

    ServiceHandle service (::OpenServiceW (manager.get(), brand::serviceName, SERVICE_ALL_ACCESS));
    if (service)
    {
        // Upgrade in place: keep the registration, refresh its configuration.
        if (! ::ChangeServiceConfigW (service.get(), SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                      commandLine.toWideCharPointer(), nullptr, nullptr, L"\0", L"LocalSystem", nullptr,
                                      brand::serviceDisplayName))
        {
            error = win::lastErrorText();
            return false;
        }
    }
    else
    {
        if (const DWORD e = ::GetLastError(); e != ERROR_SERVICE_DOES_NOT_EXIST)
        {
            error = win::win32ErrorText (e);
            return false;
        }
        // No dependencies: the audio services may start later during boot;
        // the engine retries enumeration until they are reachable.
        service.reset (::CreateServiceW (manager.get(), brand::serviceName, brand::serviceDisplayName, SERVICE_ALL_ACCESS,
                                         SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                         commandLine.toWideCharPointer(), nullptr, nullptr, nullptr, nullptr,
                                         L"LocalSystem"));
        if (! service)
        {
            error = win::lastErrorText();
            return false;
        }
    }

    SERVICE_DESCRIPTIONW description {};
    description.lpDescription = const_cast<wchar_t*> (brand::serviceDescription);
    if (! ::ChangeServiceConfig2W (service.get(), SERVICE_CONFIG_DESCRIPTION, &description))
        warnings.add ("description: " + win::lastErrorText());

    // Recovery: restart on the first, second and later failures; the failure
    // count resets after one day without failures.
    SC_ACTION actions[3] = { { SC_ACTION_RESTART, 5000 }, { SC_ACTION_RESTART, 10000 }, { SC_ACTION_RESTART, 30000 } };
    SERVICE_FAILURE_ACTIONSW failure {};
    failure.dwResetPeriod = 86400;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    if (! ::ChangeServiceConfig2W (service.get(), SERVICE_CONFIG_FAILURE_ACTIONS, &failure))
        warnings.add ("recovery actions: " + win::lastErrorText());

    // Also restart when the service stops itself with an error exit code (not
    // only on crashes). A clean stop (exit code 0) is never restarted, which
    // is what the tray's "Encerrar" relies on.
    SERVICE_FAILURE_ACTIONS_FLAG flag {};
    flag.fFailureActionsOnNonCrashFailures = TRUE;
    if (! ::ChangeServiceConfig2W (service.get(), SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &flag))
        warnings.add ("recovery on non-crash failures: " + win::lastErrorText());

    // Give the SCM time to shut us down cleanly at system shutdown.
    SERVICE_PRESHUTDOWN_INFO preshutdown { 10000 };
    if (! ::ChangeServiceConfig2W (service.get(), SERVICE_CONFIG_PRESHUTDOWN_INFO, &preshutdown))
        warnings.add ("preshutdown timeout: " + win::lastErrorText());

    // Default service DACL plus start / stop / pause-continue for interactive
    // users, so the tray and `pause` / `resume` work without elevation.
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW (
            L"D:(A;;CCLCSWRPWPDTLOCRRC;;;SY)(A;;CCDCLCSWRPWPDTLOCRSDRCWDWO;;;BA)"
            L"(A;;CCLCSWRPWPDTLOCRRC;;;IU)(A;;CCLCSWLOCRRC;;;SU)",
            SDDL_REVISION_1, &sd, nullptr))
    {
        win::LocalMemory owner (sd);
        if (! ::SetServiceObjectSecurity (service.get(), DACL_SECURITY_INFORMATION, sd))
            warnings.add ("service permissions: " + win::lastErrorText());
    }
    else
    {
        warnings.add ("service permissions: " + win::lastErrorText());
    }
    return true;
}

bool uninstall (juce::String& error, const wchar_t* serviceName)
{
    DWORD openError = 0;
    auto service = openService (nameOrDefault (serviceName), SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE, openError);
    if (! service)
    {
        if (openError == ERROR_SERVICE_DOES_NOT_EXIST)
            return true; // nothing to remove
        error = win::win32ErrorText (openError);
        return false;
    }
    SERVICE_STATUS status {};
    if (::ControlService (service.get(), SERVICE_CONTROL_STOP, &status))
        waitForState (service.get(), SERVICE_STOPPED, 20000, status);

    if (! ::DeleteService (service.get()))
    {
        if (const DWORD e = ::GetLastError(); e != ERROR_SERVICE_MARKED_FOR_DELETE)
        {
            error = win::win32ErrorText (e);
            return false;
        }
    }
    return true;
}

bool start (juce::String& error)
{
    DWORD openError = 0;
    auto service = openService (brand::serviceName, SERVICE_START | SERVICE_QUERY_STATUS, openError);
    if (! service)
    {
        error = win::win32ErrorText (openError);
        return false;
    }
    if (! ::StartServiceW (service.get(), 0, nullptr))
    {
        if (const DWORD e = ::GetLastError(); e != ERROR_SERVICE_ALREADY_RUNNING)
        {
            error = win::win32ErrorText (e);
            return false;
        }
        return true;
    }
    SERVICE_STATUS status {};
    ::QueryServiceStatus (service.get(), &status);
    if (! waitForState (service.get(), SERVICE_RUNNING, 20000, status))
    {
        error = "the service did not start (now " + stateName (fromNative (status.dwCurrentState)) + ")";
        return false;
    }
    return true;
}

bool stop (juce::String& error, int timeoutMs, const wchar_t* serviceName)
{
    DWORD openError = 0;
    auto service = openService (nameOrDefault (serviceName), SERVICE_STOP | SERVICE_QUERY_STATUS, openError);
    if (! service)
    {
        error = win::win32ErrorText (openError);
        return false;
    }
    SERVICE_STATUS status {};
    if (! ::ControlService (service.get(), SERVICE_CONTROL_STOP, &status))
    {
        if (const DWORD e = ::GetLastError(); e != ERROR_SERVICE_NOT_ACTIVE)
        {
            error = win::win32ErrorText (e);
            return false;
        }
        return true;
    }
    if (! waitForState (service.get(), SERVICE_STOPPED, timeoutMs, status))
    {
        error = "the service did not stop in time";
        return false;
    }
    return true;
}

bool restart (juce::String& error)
{
    return stop (error) && start (error);
}

bool pause (juce::String& error)
{
    return control (SERVICE_CONTROL_PAUSE, SERVICE_PAUSED, SERVICE_PAUSE_CONTINUE, error);
}

bool resume (juce::String& error)
{
    return control (SERVICE_CONTROL_CONTINUE, SERVICE_RUNNING, SERVICE_PAUSE_CONTINUE, error);
}
} // namespace audioslave::scm
