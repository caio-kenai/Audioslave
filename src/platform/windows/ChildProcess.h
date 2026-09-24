#pragma once
// A child process whose lifetime is bound to the parent through a Windows job
// object (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE): if the parent exits or crashes
// the child is terminated by the kernel, so it can never be left orphaned.
// Used by the tray to run the portable monitoring host.

#include <juce_core/juce_core.h>

#include <memory>

namespace audioslave::win
{
class ChildProcess
{
public:
    ChildProcess();
    ~ChildProcess(); // terminates the child if it is still running

    ChildProcess (const ChildProcess&) = delete;
    ChildProcess& operator= (const ChildProcess&) = delete;

    // Starts `executable arguments...` without a window. False + error on failure.
    bool launch (const juce::File& executable, const juce::StringArray& arguments, juce::String& error);

    [[nodiscard]] bool isRunning() const;

    // Waits up to `timeoutMs` for the child to exit; true when it has exited.
    bool waitForExit (int timeoutMs) const;

    // Kills the child immediately (job termination).
    void terminate();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace audioslave::win
