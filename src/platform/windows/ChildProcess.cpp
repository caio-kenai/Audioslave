#include "platform/windows/WinCommon.h"
#include "platform/windows/ChildProcess.h"
#include "platform/windows/Elevation.h"
#include "platform/windows/WinError.h"
#include "platform/windows/WinHandles.h"

namespace audioslave::win
{
struct ChildProcess::Impl
{
    UniqueHandle job;
    UniqueHandle process;
};

ChildProcess::ChildProcess() : impl_ (std::make_unique<Impl>()) {}

ChildProcess::~ChildProcess()
{
    terminate();
}

bool ChildProcess::launch (const juce::File& executable, const juce::StringArray& arguments, juce::String& error)
{
    terminate();

    UniqueHandle job (::CreateJobObjectW (nullptr, nullptr));
    if (! job)
    {
        error = "CreateJobObject: " + lastErrorText();
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (! ::SetInformationJobObject (job.get(), JobObjectExtendedLimitInformation, &limits, sizeof (limits)))
    {
        error = "SetInformationJobObject: " + lastErrorText();
        return false;
    }

    juce::String commandLine = quoteArgument (executable.getFullPathName());
    for (const auto& a : arguments)
        commandLine << " " << quoteArgument (a);
    std::wstring mutableCommandLine (commandLine.toWideCharPointer());

    STARTUPINFOW startup {};
    startup.cb = sizeof (startup);
    PROCESS_INFORMATION info {};
    // Suspended until it is inside the job, so it can never escape it.
    if (! ::CreateProcessW (executable.getFullPathName().toWideCharPointer(), mutableCommandLine.data(), nullptr,
                            nullptr, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                            executable.getParentDirectory().getFullPathName().toWideCharPointer(), &startup, &info))
    {
        error = "CreateProcess: " + lastErrorText();
        return false;
    }
    UniqueHandle process (info.hProcess);
    UniqueHandle thread (info.hThread);

    if (! ::AssignProcessToJobObject (job.get(), process.get()))
    {
        error = "AssignProcessToJobObject: " + lastErrorText();
        ::TerminateProcess (process.get(), 1);
        return false;
    }
    ::ResumeThread (thread.get());

    impl_->job = std::move (job);
    impl_->process = std::move (process);
    return true;
}

bool ChildProcess::isRunning() const
{
    return impl_->process && ::WaitForSingleObject (impl_->process.get(), 0) == WAIT_TIMEOUT;
}

bool ChildProcess::waitForExit (int timeoutMs) const
{
    if (! impl_->process)
        return true;
    return ::WaitForSingleObject (impl_->process.get(), timeoutMs < 0 ? INFINITE : static_cast<DWORD> (timeoutMs))
           == WAIT_OBJECT_0;
}

void ChildProcess::terminate()
{
    if (impl_->job)
        ::TerminateJobObject (impl_->job.get(), 1);
    impl_->process.reset();
    impl_->job.reset();
}
} // namespace audioslave::win
